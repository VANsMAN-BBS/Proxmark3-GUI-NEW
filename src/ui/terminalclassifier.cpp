#include "terminalclassifier.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace
{
struct QuotePair
{
    QChar open;
    QChar close;
};

const QVector<QuotePair> &quotePairs()
{
    static const QVector<QuotePair> pairs = {
        { QLatin1Char('`'), QLatin1Char('`') },
        { QLatin1Char('"'), QLatin1Char('"') },
        { QLatin1Char('\''), QLatin1Char('\'') },
        { QChar(0x201C), QChar(0x201D) }, // “ ”
        { QChar(0x2018), QChar(0x2019) }, // ‘ ’
        { QChar(0x201E), QChar(0x201C) }, // „ “
        { QChar(0x300C), QChar(0x300D) }, // 「 」
        { QChar(0x300E), QChar(0x300F) }, // 『 』
        { QChar(0x00AB), QChar(0x00BB) }, // « »
        { QChar(0x2039), QChar(0x203A) }  // ‹ ›
    };
    return pairs;
}

QStringList commandTokens(const QString &command)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return command.trimmed().split(whitespace, Qt::SkipEmptyParts);
}

bool prefixMatches(const QStringList &candidate, const QStringList &catalog)
{
    if (catalog.isEmpty() || candidate.size() < catalog.size()) {
        return false;
    }
    for (int i = 0; i < catalog.size(); ++i) {
        if (candidate.at(i).compare(catalog.at(i), Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return true;
}

bool loadCatalog(QHash<QString, QVector<QStringList> > *commands)
{
    QFile file(QStringLiteral(":/commands/commands.json"));
    if (!file.open(QFile::ReadOnly)) {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonArray entries = document.object().value(QStringLiteral("commands")).toArray();
    for (const QJsonValue &value : entries) {
        const QString command =
            value.toObject().value(QStringLiteral("command")).toString().trimmed();
        const QStringList tokens = commandTokens(command);
        if (tokens.isEmpty()) {
            continue;
        }
        commands->operator[](tokens.first().toLower()).append(tokens);
    }
    return !commands->isEmpty();
}

const QHash<QString, QVector<QStringList> > &catalogCommands()
{
    static QHash<QString, QVector<QStringList> > commands;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (!loadCatalog(&commands)) {
            commands.clear();
        }
    }
    return commands;
}

const QSet<QString> &topLevelCommands()
{
    // Fallback only: production builds and the terminal test link
    // :/commands/commands.json, which is the authoritative 1024-entry list.
    static const QSet<QString> known = {
        QStringLiteral("analyse"), QStringLiteral("auto"), QStringLiteral("clear"),
        QStringLiteral("data"), QStringLiteral("emv"), QStringLiteral("exit"),
        QStringLiteral("help"), QStringLiteral("hf"), QStringLiteral("hints"),
        QStringLiteral("hw"), QStringLiteral("lf"), QStringLiteral("mad"),
        QStringLiteral("mem"), QStringLiteral("mqtt"), QStringLiteral("msleep"),
        QStringLiteral("nfc"), QStringLiteral("piv"), QStringLiteral("prefs"),
        QStringLiteral("quit"), QStringLiteral("rem"), QStringLiteral("reveng"),
        QStringLiteral("script"), QStringLiteral("smart"), QStringLiteral("trace"),
        QStringLiteral("usart"), QStringLiteral("wiegand")
    };
    return known;
}

void appendHint(QVector<TerminalHintSpan> *spans, int start, int length,
                const QString &command)
{
    for (const TerminalHintSpan &existing : *spans) {
        if (existing.start == start && existing.length == length) {
            return;
        }
    }
    TerminalHintSpan span;
    span.start = start;
    span.length = length;
    span.command = command;
    spans->append(span);
}

void scanQuotedCommand(const QString &line, int from, int lineOffset,
                       const QuotePair &pair, QVector<TerminalHintSpan> *spans)
{
    int search = from;
    while (search < line.length()) {
        const int open = line.indexOf(pair.open, search);
        if (open < 0) {
            return;
        }
        const int close = line.indexOf(pair.close, open + 1);
        if (close < 0) {
            return;
        }

        int first = open + 1;
        int last = close - 1;
        while (first <= last && line.at(first).isSpace()) {
            ++first;
        }
        while (last >= first && line.at(last).isSpace()) {
            --last;
        }

        if (last >= first) {
            const QString command = line.mid(first, last - first + 1);
            if (TerminalClassifier::isKnownCommandStart(command)) {
                appendHint(spans, lineOffset + first, command.length(), command);
            }
        }
        search = close + 1;
    }
}
}

QString TerminalClassifier::stripAnsi(const QString &text)
{
    static const QRegularExpression ansi(
        QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1B\\\\)|[@-_])"));
    QString result = text;
    result.remove(ansi);
    return result;
}

TerminalLineClass TerminalClassifier::classifyLine(const QString &text)
{
    QString line = stripAnsi(text);
    line.remove(QLatin1Char('\r'));

    // Command echo produced by the client, e.g. "[usb] pm3 --> hf 14a info"
    // or "[offline|script] pm3 --> help".
    static const QRegularExpression prompt(
        QStringLiteral("^\\s*(?:\\[[^\\]\\n]*\\]\\s*)?pm3\\s*-->"),
        QRegularExpression::CaseInsensitiveOption);
    if (prompt.match(line).hasMatch()) {
        return TerminalLineClass::UserCommand;
    }

    // Exact marker + boundary: the client always prints "[X]" followed by a
    // space (or the end of the line).  Longest marker first: [!!] wins over [!].
    static const QRegularExpression marker(
        QStringLiteral("^\\s*\\[(!!|!|\\+|\\#|\\?|=|-|\\||/|\\\\|\\*)\\](?=\\s|$)"));
    const QRegularExpressionMatch markerMatch = marker.match(line);
    if (markerMatch.hasMatch()) {
        const QString token = markerMatch.captured(1);
        if (token == QLatin1String("!!") || token == QLatin1String("-")) {
            if (token == QLatin1String("-")
                && isInPlaceProgress(line.mid(markerMatch.capturedEnd()))) {
                return TerminalLineClass::Progress;
            }
            return TerminalLineClass::Error;
        }
        if (token == QLatin1String("!")) {
            return TerminalLineClass::Warning;
        }
        if (token == QLatin1String("+")) {
            return TerminalLineClass::Success;
        }
        if (token == QLatin1String("=") || token == QLatin1String("*")) {
            return TerminalLineClass::Info;
        }
        if (token == QLatin1String("#")) {
            return TerminalLineClass::Data;
        }
        if (token == QLatin1String("?")) {
            return TerminalLineClass::Hint;
        }
        if (token == QLatin1String("|") || token == QLatin1String("/")
            || token == QLatin1String("\\")) {
            return TerminalLineClass::Progress;
        }
    }

    const QVector<TerminalSpan> fields = semanticSpans(line);
    if (!fields.isEmpty()) {
        return fields.first().lineClass;
    }

    // Plain output stays uncoloured on purpose.
    return TerminalLineClass::Plain;
}

bool TerminalClassifier::isInPlaceProgress(const QString &textAfterMarker)
{
    const QString message = textAfterMarker.trimmed();
    if (message.isEmpty()) {
        return false;
    }

    // Anchored forms taken from the client's PrintAndLogEx(INPLACE, ...)
    // call sites.  Do not broaden this to "contains" checks: FAILED messages
    // also mention progress words and end with "...".
    static const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral("^Searching for .+\\.\\.\\.$")),
        QRegularExpression(QStringLiteral("^Reading sector\\s+\\d+\\s*/\\s*\\d+")),
        QRegularExpression(QStringLiteral("^Quick-reading sector\\s+\\d+\\s*/\\s*\\d+")),
        QRegularExpression(QStringLiteral("^Writing page\\b")),
        QRegularExpression(QStringLiteral("^Wiping block\\s+\\d+")),
        QRegularExpression(QStringLiteral("^Restoring block\\s+\\d+")),
        QRegularExpression(QStringLiteral("^Formatting block\\s+\\d+")),
        QRegularExpression(QStringLiteral("^Block\\s+\\d+\\s*/\\s*\\d+")),
        QRegularExpression(QStringLiteral("^Testing\\s+\\d+\\s*/\\s*\\d+")),
        QRegularExpression(QStringLiteral("^Checking Keys\\s+\\d+\\s*/\\s*\\d+")),
        QRegularExpression(QStringLiteral("^Remaining keys:\\s*\\d+")),
        QRegularExpression(QStringLiteral("^Progress:\\s*\\d+\\s*%")),
        QRegularExpression(QStringLiteral("^Brute DESFire .+ Progress")),
        QRegularExpression(QStringLiteral("^Bruteforcing range")),
        QRegularExpression(QStringLiteral("^Chunk \\[\\d+/\\d+\\]")),
        QRegularExpression(QStringLiteral("^Attack \\d+ running\\.\\.\\.$")),
        QRegularExpression(QStringLiteral("^Tear off delay")),
        QRegularExpression(QStringLiteral("^E-paper Reflashing, Waiting")),
        QRegularExpression(QStringLiteral("^Delay:")),
        QRegularExpression(QStringLiteral("^Sector\\.\\.\\."))
    };
    for (const QRegularExpression &pattern : patterns) {
        if (pattern.match(message).hasMatch()) {
            return true;
        }
    }
    return false;
}

QVector<TerminalSpan> TerminalClassifier::semanticSpans(const QString &text)
{
    QVector<TerminalSpan> spans;

    static const QRegularExpression protocol(
        QStringLiteral("\\b(?:UID|ATQA|SAK)\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hardware(
        QStringLiteral("^\\s*(?:\\[[^\\]\\n]*\\]\\s*)?(Firmware|Model)\\b\\s*[.:]"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = protocol.match(text);
    if (match.hasMatch()) {
        TerminalSpan span;
        span.start = match.capturedStart();
        span.length = text.length() - span.start;
        span.lineClass = TerminalLineClass::Protocol;
        spans.append(span);
    }

    match = hardware.match(text);
    if (match.hasMatch()) {
        // A line normally carries either a protocol or a hardware field.  If
        // both are present, keep the earlier one first and append the other.
        TerminalSpan span;
        span.start = match.capturedStart(1);
        span.length = text.length() - span.start;
        span.lineClass = TerminalLineClass::Hardware;
        if (spans.isEmpty() || spans.first().start != span.start) {
            spans.append(span);
        }
    }

    return spans;
}

QVector<TerminalHintSpan> TerminalClassifier::hintSpans(const QString &text)
{
    QVector<TerminalHintSpan> spans;
    const QString content = stripAnsi(text);

    static const QRegularExpression hintLine(
        QStringLiteral("^\\s*\\[\\?\\]\\s*Hint\\s*:\\s*Try\\b"),
        QRegularExpression::CaseInsensitiveOption);

    int lineOffset = 0;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = hintLine.match(line);
        if (match.hasMatch()) {
            const int from = match.capturedEnd();
            for (const QuotePair &pair : quotePairs()) {
                scanQuotedCommand(line, from, lineOffset, pair, &spans);
            }
        }
        lineOffset += line.length() + 1; // Qt splits on '\n'
    }

    // Stable order, then remove overlapping matches (an inner command can be
    // enclosed by a second set of quotes only in malformed text).
    std::sort(spans.begin(), spans.end(), [](const TerminalHintSpan &first,
                                             const TerminalHintSpan &second) {
        if (first.start != second.start) {
            return first.start < second.start;
        }
        return first.length > second.length;
    });

    QVector<TerminalHintSpan> filtered;
    int end = -1;
    for (const TerminalHintSpan &span : spans) {
        if (span.start < end) {
            continue;
        }
        filtered.append(span);
        end = span.start + span.length;
    }
    return filtered;
}

QString TerminalClassifier::hintAt(const QString &text, int offset)
{
    const QVector<TerminalHintSpan> hints = hintSpans(text);
    for (const TerminalHintSpan &hint : hints) {
        if (offset >= hint.start && offset <= hint.start + hint.length) {
            return hint.command;
        }
    }
    return QString();
}

bool TerminalClassifier::isKnownCommandStart(const QString &command)
{
    const QString cleaned = command.trimmed();
    if (cleaned.isEmpty() || cleaned.startsWith(QLatin1Char('-'))) {
        return false;
    }

    const QStringList tokens = commandTokens(cleaned);
    if (tokens.isEmpty() || tokens.first().startsWith(QLatin1Char('-'))) {
        return false;
    }

    const QHash<QString, QVector<QStringList> > &catalog = catalogCommands();
    if (!catalog.isEmpty()) {
        const QVector<QStringList> candidates = catalog.value(tokens.first().toLower());
        for (const QStringList &candidate : candidates) {
            if (prefixMatches(tokens, candidate)) {
                return true;
            }
        }
        return false;
    }

    return topLevelCommands().contains(tokens.first().toLower());
}

QString TerminalClassifier::className(TerminalLineClass lineClass)
{
    switch (lineClass) {
    case TerminalLineClass::Plain: return QStringLiteral("plain");
    case TerminalLineClass::UserCommand: return QStringLiteral("user");
    case TerminalLineClass::Success: return QStringLiteral("success");
    case TerminalLineClass::Info: return QStringLiteral("info");
    case TerminalLineClass::Data: return QStringLiteral("data");
    case TerminalLineClass::Progress: return QStringLiteral("progress");
    case TerminalLineClass::Hardware: return QStringLiteral("hardware");
    case TerminalLineClass::Protocol: return QStringLiteral("protocol");
    case TerminalLineClass::Hint: return QStringLiteral("hint");
    case TerminalLineClass::Warning: return QStringLiteral("warning");
    case TerminalLineClass::Error: return QStringLiteral("error");
    }
    return QStringLiteral("plain");
}
