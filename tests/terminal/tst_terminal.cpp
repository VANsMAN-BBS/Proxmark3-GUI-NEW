#include <QtTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include "ui/terminalclassifier.h"
#include "ui/terminalhighlighter.h"
#include "ui/terminalpalette.h"

class TestTerminal : public QObject
{
    Q_OBJECT

private slots:
    void paletteHasClassicGreenMapping();
    void paletteHasReadableContrast();
    void paletteLightGreenCandidatesStayReadable();
    void classifiesMarkers();
    void classifiesInPlaceProgress();
    void progressMarkerIsNotAWarningOrError();
    void errorMarkerWinsOverWarning();
    void markerNeedsExactBoundary();
    void markerLookalikesStayPlain();
    void classifiesAnsiWrappedMarkers();
    void classifiesPartialMarkersAsPlain();
    void classifiesSemanticFields();
    void plainTextStaysPlain();
    void partialLinesStayPlain();
    void stripsAnsiForAnalysisOnly();
    void findsSingleHint();
    void findsMultipleHints();
    void findsQuoteVariants();
    void findsUnicodeQuoteVariants();
    void findsMultipleHintsAcrossSplitBuffers();
    void hitTestsHintOffsets();
    void validatesParameterizedCommandsAgainstCatalog();
    void rejectsUncataloguedCommands();
    void ignoresOrdinaryQuotedTextAndPaths();
    void ignoresOptionOnlyHint();
    void ignoresMalformedHints();
    void highlighterKeepsPlainTextAndStyles();
    void highlighterStylesProgressNotError();
    void highlighterSwitchesTheme();
};

static QString colourName(const QColor &colour)
{
    return colour.name(QColor::HexRgb).toLower();
}

void TestTerminal::paletteHasClassicGreenMapping()
{
    const TerminalPalette dark = TerminalPalette::forDark(true);
    const TerminalPalette light = TerminalPalette::forDark(false);

    QCOMPARE(colourName(dark.normal), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.userCommand), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.success), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.info), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.data), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.progress), QStringLiteral("#00d68f"));
    QCOMPARE(colourName(dark.hardware), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.protocol), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.hint), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.hintCommand), QStringLiteral("#39ff14"));
    QCOMPARE(colourName(dark.warning), QStringLiteral("#ffd54f"));
    QCOMPARE(colourName(dark.error), QStringLiteral("#ff5252"));

    QCOMPARE(colourName(light.normal), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.userCommand), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.success), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.info), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.data), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.progress), QStringLiteral("#00796b"));
    QCOMPARE(colourName(light.hardware), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.protocol), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.hint), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.hintCommand), QStringLiteral("#008000"));
    QCOMPARE(colourName(light.warning), QStringLiteral("#946000"));
    QCOMPARE(colourName(light.error), QStringLiteral("#c62828"));
}

void TestTerminal::paletteHasReadableContrast()
{
    const TerminalPalette dark = TerminalPalette::forDark(true);
    const TerminalPalette light = TerminalPalette::forDark(false);
    const QColor darkBackground(QStringLiteral("#19232D"));
    const QColor lightBackground(QStringLiteral("#FAFAFA"));

    const QList<QColor> darkColours = {
        dark.normal, dark.userCommand, dark.success, dark.info, dark.data,
        dark.progress, dark.hardware, dark.protocol, dark.hint, dark.hintCommand,
        dark.warning, dark.error
    };
    for (const QColor &colour : darkColours) {
        const qreal ratio = TerminalPalette::contrastRatio(colour, darkBackground);
        QVERIFY2(ratio >= 4.5,
                 qPrintable(QStringLiteral("dark %1 contrast %2")
                                .arg(colourName(colour))
                                .arg(ratio)));
    }

    const QList<QColor> lightColours = {
        light.normal, light.userCommand, light.success, light.info, light.data,
        light.progress, light.hardware, light.protocol, light.hint, light.hintCommand,
        light.warning, light.error
    };
    for (const QColor &colour : lightColours) {
        const qreal ratio = TerminalPalette::contrastRatio(colour, lightBackground);
        QVERIFY2(ratio >= 4.5,
                 qPrintable(QStringLiteral("light %1 contrast %2")
                                .arg(colourName(colour))
                                .arg(ratio)));
    }
}

void TestTerminal::paletteLightGreenCandidatesStayReadable()
{
    // Task: test #008000/#009900/#00A000/#008A00/#00AA00 against the real
    // light terminal background.  Only #008000 clears the 4.5:1 normal-text
    // threshold, so it remains the light-theme normal green.
    const QColor background(QStringLiteral("#FAFAFA"));
    const QList<QPair<QString, qreal> > candidates = {
        { QStringLiteral("#008000"), TerminalPalette::contrastRatio(QColor(QStringLiteral("#008000")), background) },
        { QStringLiteral("#009900"), TerminalPalette::contrastRatio(QColor(QStringLiteral("#009900")), background) },
        { QStringLiteral("#00A000"), TerminalPalette::contrastRatio(QColor(QStringLiteral("#00A000")), background) },
        { QStringLiteral("#008A00"), TerminalPalette::contrastRatio(QColor(QStringLiteral("#008A00")), background) },
        { QStringLiteral("#00AA00"), TerminalPalette::contrastRatio(QColor(QStringLiteral("#00AA00")), background) }
    };

    QString best;
    qreal bestRatio = 0.0;
    for (const QPair<QString, qreal> &candidate : candidates) {
        if (candidate.second > bestRatio) {
            bestRatio = candidate.second;
            best = candidate.first;
        }
        if (candidate.first == QLatin1String("#008000")) {
            QVERIFY2(candidate.second >= 4.5,
                     qPrintable(QStringLiteral("#008000 contrast %1").arg(candidate.second)));
        } else {
            QVERIFY2(candidate.second < 4.5,
                     qPrintable(QStringLiteral("%1 unexpectedly passes: %2")
                                    .arg(candidate.first)
                                    .arg(candidate.second)));
        }
    }
    QCOMPARE(best, QStringLiteral("#008000"));
    QVERIFY(bestRatio >= 4.5);

    const TerminalPalette light = TerminalPalette::forDark(false);
    QCOMPARE(colourName(light.normal), best);
    // Warning and error stay distinct from the normal green.
    QVERIFY(light.warning != light.normal);
    QVERIFY(light.error != light.normal);
    QVERIFY(light.progress != light.normal);
}

void TestTerminal::classifiesMarkers()
{
    // Every marker the Iceman client can emit (ui.c PrintAndLogEx levels).
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[+] ok")),
             TerminalLineClass::Success);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[=] info")),
             TerminalLineClass::Info);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[#] debug")),
             TerminalLineClass::Data);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[?] Hint: Try `hf 14a info`")),
             TerminalLineClass::Hint);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!] warning")),
             TerminalLineClass::Warning);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!!] error")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-] failed to select card")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[*] neutral marker")),
             TerminalLineClass::Info);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("  [+] indented")),
             TerminalLineClass::Success);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[usb] pm3 --> hf 14a info")),
             TerminalLineClass::UserCommand);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[offline|script] pm3 --> help")),
             TerminalLineClass::UserCommand);
}

void TestTerminal::classifiesInPlaceProgress()
{
    // ui.c spinner frames: [\], [|], [/] and the ambiguous [-].
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[\\] Searching for ISO14443-A tag...")),
             TerminalLineClass::Progress);
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[|] Searching for ISO15693 tag...")),
             TerminalLineClass::Progress);
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[/] Searching for FeliCa tag...")),
             TerminalLineClass::Progress);
    // Reported bug: this is a spinner line, not a FAILED line.
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[-] Searching for ISO14443-B tag...")),
             TerminalLineClass::Progress);

    // Other in-place forms from the client's INPLACE call sites.
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-] Reading sector   1 /  16")),
             TerminalLineClass::Progress);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-] Progress: 42 %")),
             TerminalLineClass::Progress);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-] Chunk [003/128]")),
             TerminalLineClass::Progress);
}

void TestTerminal::progressMarkerIsNotAWarningOrError()
{
    const TerminalPalette dark = TerminalPalette::forDark(true);
    const TerminalPalette light = TerminalPalette::forDark(false);
    QVERIFY(dark.progress != dark.error);
    QVERIFY(dark.progress != dark.warning);
    QVERIFY(light.progress != light.error);
    QVERIFY(light.progress != light.warning);

    // Real FAILED lines must stay errors even though they mention progress
    // words or end with an ellipsis.
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[-] failed to select card, exiting...")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[-] Deobsfuscate failed, exiting...")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(
                 QStringLiteral("[-] Tag did not respond to a test APDU (select file command). Aborting...")),
             TerminalLineClass::Error);
}

void TestTerminal::errorMarkerWinsOverWarning()
{
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!!] hard failure")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!] soft failure")),
             TerminalLineClass::Warning);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!!!] not a marker")),
             TerminalLineClass::Plain);
}

void TestTerminal::markerNeedsExactBoundary()
{
    // The client always prints a space after the marker.
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[+]")),
             TerminalLineClass::Success);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-]")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[|]")),
             TerminalLineClass::Progress);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!!]\tTabbed")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("\r[-] Searching for ISO14443-B tag...")),
             TerminalLineClass::Progress);
}

void TestTerminal::markerLookalikesStayPlain()
{
    // No space/boundary after the bracket, or the marker is not at line start.
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[+]ok")),
             TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-]failed")),
             TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("see [+] ok")),
             TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[-")),
             TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[--] text")),
             TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[ + ] text")),
             TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("The [+] marker is documented")),
             TerminalLineClass::Plain);
    // A plain sentence containing "error" is not an error line.
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("no error found in this dump")),
             TerminalLineClass::Plain);
}

void TestTerminal::classifiesAnsiWrappedMarkers()
{
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("\x1b[31m[!!] bad\x1b[0m")),
             TerminalLineClass::Error);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("\x1b[33m[-] Searching for ISO14443-B tag...\x1b[0m")),
             TerminalLineClass::Progress);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("\x1b[36m[!] careful\x1b[0m")),
             TerminalLineClass::Warning);
}

void TestTerminal::classifiesPartialMarkersAsPlain()
{
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[")), TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!")), TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!!")), TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[?]")), TerminalLineClass::Hint);
}

void TestTerminal::classifiesSemanticFields()
{
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("UID: 04 A2 33 11")),
             TerminalLineClass::Protocol);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("ATQA: 00 04")),
             TerminalLineClass::Protocol);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("SAK: 20 [1]")),
             TerminalLineClass::Protocol);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("Firmware.... Iceman/master/v4.23346")),
             TerminalLineClass::Hardware);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("Model: PM3 Generic")),
             TerminalLineClass::Hardware);

    // A success line remains success, but its UID field is still exposed for
    // the per-span override in the highlighter.
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[+] UID: 04 A2 33 11")),
             TerminalLineClass::Success);
    const QVector<TerminalSpan> spans =
        TerminalClassifier::semanticSpans(QStringLiteral("[+] UID: 04 A2 33 11"));
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().lineClass, TerminalLineClass::Protocol);
    QCOMPARE(spans.first().start, 4);
}

void TestTerminal::plainTextStaysPlain()
{
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("This is ordinary output")),
             TerminalLineClass::Plain);
    QVERIFY(TerminalClassifier::semanticSpans(QStringLiteral("This is ordinary output")).isEmpty());
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("The model. is old")),
             TerminalLineClass::Plain);
    QVERIFY(TerminalClassifier::semanticSpans(QStringLiteral("The model. is old")).isEmpty());
}

void TestTerminal::partialLinesStayPlain()
{
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[!!")), TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QStringLiteral("[")), TerminalLineClass::Plain);
    QCOMPARE(TerminalClassifier::classifyLine(QString()), TerminalLineClass::Plain);
}

void TestTerminal::stripsAnsiForAnalysisOnly()
{
    const QString input = QStringLiteral("\x1b[32m[+] ok\x1b[0m");
    QCOMPARE(TerminalClassifier::classifyLine(input), TerminalLineClass::Success);
    QCOMPARE(TerminalClassifier::stripAnsi(input), QStringLiteral("[+] ok"));
    // The original string is untouched.
    QVERIFY(input.contains(QChar(0x1b)));
}

void TestTerminal::findsSingleHint()
{
    const QString line = QStringLiteral("[?] Hint: Try `hf 14a info` to inspect");
    const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(line);
    QCOMPARE(hints.size(), 1);
    QCOMPARE(hints.first().command, QStringLiteral("hf 14a info"));
    QCOMPARE(line.mid(hints.first().start, hints.first().length),
             QStringLiteral("hf 14a info"));
}

void TestTerminal::findsMultipleHints()
{
    const QString line =
        QStringLiteral("[?] Hint: Try `data load -f <file>` or `lf read` to get samples");
    const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(line);
    QCOMPARE(hints.size(), 2);
    QCOMPARE(hints.at(0).command, QStringLiteral("data load -f <file>"));
    QCOMPARE(hints.at(1).command, QStringLiteral("lf read"));
}

void TestTerminal::findsQuoteVariants()
{
    const QList<QChar> quotes = {
        QLatin1Char('`'), QLatin1Char('"'), QLatin1Char('\'')
    };
    for (const QChar quote : quotes) {
        const QString line = QStringLiteral("[?] Hint: Try %1hf 14a info%1")
                                 .arg(quote);
        const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(line);
        QVERIFY2(hints.size() == 1, qPrintable(line));
        QCOMPARE(hints.first().command, QStringLiteral("hf 14a info"));
        QCOMPARE(line.mid(hints.first().start, hints.first().length),
                 QStringLiteral("hf 14a info"));
    }
}

void TestTerminal::findsUnicodeQuoteVariants()
{
    const QList<QPair<QChar, QChar> > pairs = {
        { QChar(0x201C), QChar(0x201D) }, // “ ”
        { QChar(0x2018), QChar(0x2019) }, // ‘ ’
        { QChar(0x201E), QChar(0x201C) }, // „ “
        { QChar(0x300C), QChar(0x300D) }, // 「 」
        { QChar(0x300E), QChar(0x300F) }, // 『 』
        { QChar(0x00AB), QChar(0x00BB) }, // « »
        { QChar(0x2039), QChar(0x203A) }  // ‹ ›
    };
    for (const QPair<QChar, QChar> &pair : pairs) {
        const QString line = QStringLiteral("[?] Hint: Try %1hf 14a info%2")
                                 .arg(pair.first)
                                 .arg(pair.second);
        const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(line);
        QVERIFY2(hints.size() == 1, qPrintable(line));
        QCOMPARE(hints.first().command, QStringLiteral("hf 14a info"));
        QCOMPARE(line.mid(hints.first().start, hints.first().length),
                 QStringLiteral("hf 14a info"));
    }

    const QString mixed =
        QStringLiteral("[?] Hint: Try “hf 14a info” or ‘lf read’ then 「hf mf info」");
    const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(mixed);
    QCOMPARE(hints.size(), 3);
    QCOMPARE(hints.at(0).command, QStringLiteral("hf 14a info"));
    QCOMPARE(hints.at(1).command, QStringLiteral("lf read"));
    QCOMPARE(hints.at(2).command, QStringLiteral("hf mf info"));
}

void TestTerminal::findsMultipleHintsAcrossSplitBuffers()
{
    const QString text =
        QStringLiteral("[?] Hint: Try `hf 14a info`\n[?] Hint: Try 'lf read'");
    const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(text);
    QCOMPARE(hints.size(), 2);
    QCOMPARE(hints.at(0).command, QStringLiteral("hf 14a info"));
    QCOMPARE(hints.at(1).command, QStringLiteral("lf read"));
    QCOMPARE(hints.at(1).start, text.indexOf(QStringLiteral("lf read")));
    QCOMPARE(text.mid(hints.at(1).start, hints.at(1).length),
             QStringLiteral("lf read"));
}

void TestTerminal::hitTestsHintOffsets()
{
    const QString line = QStringLiteral("[?] Hint: Try “hf 14a info” or 'lf read'");
    const int first = line.indexOf(QStringLiteral("hf 14a info"));
    const int second = line.indexOf(QStringLiteral("lf read"));

    QCOMPARE(TerminalClassifier::hintAt(line, first), QStringLiteral("hf 14a info"));
    QCOMPARE(TerminalClassifier::hintAt(line, first + 4), QStringLiteral("hf 14a info"));
    QCOMPARE(TerminalClassifier::hintAt(line, second), QStringLiteral("lf read"));
    QCOMPARE(TerminalClassifier::hintAt(line, second + 2), QStringLiteral("lf read"));
    QVERIFY(TerminalClassifier::hintAt(line, 0).isEmpty());
    QVERIFY(TerminalClassifier::hintAt(QStringLiteral("ordinary text"), 3).isEmpty());
}

void TestTerminal::validatesParameterizedCommandsAgainstCatalog()
{
    QVERIFY(TerminalClassifier::isKnownCommandStart(QStringLiteral("hf 14a info")));
    QVERIFY(TerminalClassifier::isKnownCommandStart(QStringLiteral("hf mf rdbl --blk 0 -k FFFFFFFFFFFF")));
    QVERIFY(TerminalClassifier::isKnownCommandStart(QStringLiteral("data load -f dump.bin")));
    QVERIFY(TerminalClassifier::isKnownCommandStart(QStringLiteral("hw version")));

    const QString line = QStringLiteral(
        "[?] Hint: Try \"hf mf rdbl --blk 0 -k FFFFFFFFFFFF\" to read a block");
    const QVector<TerminalHintSpan> hints = TerminalClassifier::hintSpans(line);
    QCOMPARE(hints.size(), 1);
    QCOMPARE(hints.first().command,
             QStringLiteral("hf mf rdbl --blk 0 -k FFFFFFFFFFFF"));
}

void TestTerminal::rejectsUncataloguedCommands()
{
    QVERIFY(!TerminalClassifier::isKnownCommandStart(QStringLiteral("hf definitelynotacommand")));
    QVERIFY(!TerminalClassifier::isKnownCommandStart(QStringLiteral("--size")));
    QVERIFY(TerminalClassifier::hintSpans(
                QStringLiteral("[?] Hint: Try `hf definitelynotacommand`")).isEmpty());
}

void TestTerminal::ignoresOrdinaryQuotedTextAndPaths()
{
    QVERIFY(TerminalClassifier::hintSpans(
                QStringLiteral("[?] Hint: Try \"C:\\Users\\van\\dump.bin\"")).isEmpty());
    QVERIFY(TerminalClassifier::hintSpans(
                QStringLiteral("[?] Hint: Try 'the card is not present'")).isEmpty());
    QVERIFY(TerminalClassifier::hintSpans(
                QStringLiteral("[?] Hint: Try “unknown error text”")).isEmpty());
    QVERIFY(TerminalClassifier::hintSpans(
                QStringLiteral("ordinary \"hf 14a info\" text is not a hint")).isEmpty());
}

void TestTerminal::ignoresOptionOnlyHint()
{
    // `--size` is an option, not an executable command.
    QCOMPARE(TerminalClassifier::hintSpans(
                 QStringLiteral("[?] Hint: Try a smaller `--size`")).size(),
             0);
}

void TestTerminal::ignoresMalformedHints()
{
    QCOMPARE(TerminalClassifier::hintSpans(
                 QStringLiteral("[?] Hint: Try `hf 14a info")).size(),
             0); // unclosed
    QCOMPARE(TerminalClassifier::hintSpans(
                 QStringLiteral("[?] Hint: `hf 14a info`")).size(),
             0); // no Try
    QCOMPARE(TerminalClassifier::hintSpans(
                 QStringLiteral("[=] Hint: Try `hf 14a info`")).size(),
             0); // wrong marker
    QCOMPARE(TerminalClassifier::hintSpans(
                 QStringLiteral("[?] Hint: Try ``")).size(),
             0); // empty
    QCOMPARE(TerminalClassifier::hintSpans(
                 QStringLiteral("hf 14a info")).size(),
             0); // no hint context
}

void TestTerminal::highlighterKeepsPlainTextAndStyles()
{
    const QString plain = QStringLiteral("[+] ok\nordinary line\n[?] Hint: Try `hw version`");
    QTextDocument document;
    document.setPlainText(plain);

    TerminalHighlighter highlighter(&document, true);
    highlighter.rehighlight();

    // Copy semantics: formatting must never change the document text.
    QCOMPARE(document.toPlainText(), plain);

    const QTextBlock success = document.findBlockByNumber(0);
    const QTextBlock ordinary = document.findBlockByNumber(1);
    const QTextBlock hint = document.findBlockByNumber(2);
    QVERIFY(success.layout() != nullptr);
    QVERIFY(ordinary.layout() != nullptr);
    QVERIFY(hint.layout() != nullptr);

    QVERIFY(!success.layout()->formats().isEmpty());
    QCOMPARE(colourName(success.layout()->formats().first().format.foreground().color()),
             QStringLiteral("#39ff14"));
    // Ordinary output is now classic green rather than uncoloured.
    QVERIFY(!ordinary.layout()->formats().isEmpty());
    QCOMPARE(colourName(ordinary.layout()->formats().first().format.foreground().color()),
             QStringLiteral("#39ff14"));

    bool foundHintColour = false;
    bool foundUnderline = false;
    for(const QTextLayout::FormatRange &range : hint.layout()->formats())
    {
        if(colourName(range.format.foreground().color()) == QStringLiteral("#39ff14"))
            foundHintColour = true;
        if(range.format.fontUnderline())
            foundUnderline = true;
    }
    QVERIFY(foundHintColour);
    QVERIFY(foundUnderline);
}

void TestTerminal::highlighterStylesProgressNotError()
{
    const QString plain =
        QStringLiteral("[-] Searching for ISO14443-B tag...\n[|] Searching for ISO15693 tag...\n[!!] real error");
    QTextDocument document;
    document.setPlainText(plain);

    TerminalHighlighter highlighter(&document, true);
    highlighter.rehighlight();
    QCOMPARE(document.toPlainText(), plain);

    const auto firstColour = [](const QTextBlock &block) {
        return colourName(block.layout()->formats().first().format.foreground().color());
    };
    QCOMPARE(firstColour(document.findBlockByNumber(0)), QStringLiteral("#00d68f"));
    QCOMPARE(firstColour(document.findBlockByNumber(1)), QStringLiteral("#00d68f"));
    QCOMPARE(firstColour(document.findBlockByNumber(2)), QStringLiteral("#ff5252"));

    // Progress is readable on the dark background and is not italicised text
    // loss (the plain text above is unchanged).
    QVERIFY(TerminalPalette::contrastRatio(QColor(QStringLiteral("#00d68f")),
                                           QColor(QStringLiteral("#19232D"))) >= 4.5);
}

void TestTerminal::highlighterSwitchesTheme()
{
    const QString plain = QStringLiteral("[!] warning");
    QTextDocument document;
    document.setPlainText(plain);

    TerminalHighlighter highlighter(&document, true);
    highlighter.rehighlight();

    const auto containsColour = [](const QTextBlock &block, const QString &name) {
        if (!block.layout()) {
            return false;
        }
        for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
            if (colourName(range.format.foreground().color()) == name) {
                return true;
            }
        }
        return false;
    };

    QVERIFY(containsColour(document.findBlockByNumber(0), QStringLiteral("#ffd54f")));

    highlighter.setDarkBackground(false);
    QVERIFY(!highlighter.isDarkBackground());
    QVERIFY(containsColour(document.findBlockByNumber(0), QStringLiteral("#946000")));
}

QTEST_MAIN(TestTerminal)
#include "tst_terminal.moc"
