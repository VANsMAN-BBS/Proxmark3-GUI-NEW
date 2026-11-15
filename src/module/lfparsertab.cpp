#include "lfparsertab.h"

#include <lf/lfparser.h>
#include <lf/lfparserregistry.h>
#include <lf/t55xxparser.h>

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
QString valueOrMissing(const QString &text)
{
    return text.isEmpty() ? LfParserTab::tr("Not provided") : text;
}
}

LfParserTab::LfParserTab(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void LfParserTab::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    root->addWidget(splitter);

    // ---------------- input ----------------
    QGroupBox *inputBox = new QGroupBox(tr("Client text input"), splitter);
    QVBoxLayout *inputLayout = new QVBoxLayout(inputBox);
    QLabel *hint = new QLabel(
        tr("Paste Proxmark3 LF output (lf search / protocol readers) or import a "
           "text file. Parsing is pure text and works without LF-capable firmware."),
        inputBox);
    hint->setWordWrap(true);
    inputLayout->addWidget(hint);

    input = new QPlainTextEdit(inputBox);
    input->setObjectName(QStringLiteral("LF_inputEdit"));
    input->setAccessibleName(tr("Client text input"));
    input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    input->setPlaceholderText(tr("[+] EM 410x ID 0F0368568B\n[+] Valid HID Prox ID found!"));
    input->setMinimumHeight(110);
    inputLayout->addWidget(input);

    QHBoxLayout *buttons = new QHBoxLayout();
    parseButton = new QPushButton(tr("Parse"), inputBox);
    clearButton = new QPushButton(tr("Clear"), inputBox);
    importButton = new QPushButton(tr("Import text..."), inputBox);
    clientButton = new QPushButton(tr("Use RawCommand output"), inputBox);
    connect(parseButton, &QPushButton::clicked, this, &LfParserTab::parseNow);
    connect(clearButton, &QPushButton::clicked, this, &LfParserTab::clearInput);
    connect(importButton, &QPushButton::clicked, this, &LfParserTab::importTextFile);
    connect(clientButton, &QPushButton::clicked, this, &LfParserTab::requestClientText);
    buttons->addWidget(parseButton);
    buttons->addWidget(clearButton);
    buttons->addWidget(importButton);
    buttons->addWidget(clientButton);
    buttons->addStretch(1);
    inputLayout->addLayout(buttons);

    sourceLabel = new QLabel(tr("Source: manual input"), inputBox);
    sourceLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    inputLayout->addWidget(sourceLabel);
    connect(input, &QPlainTextEdit::textChanged, this, &LfParserTab::onInputChanged);

    // ---------------- result ----------------
    QGroupBox *resultBox = new QGroupBox(tr("Parsed card"), splitter);
    QVBoxLayout *resultLayout = new QVBoxLayout(resultBox);

    QHBoxLayout *statusRow = new QHBoxLayout();
    statusLabel = new QLabel(tr("No input parsed yet."), resultBox);
    statusLabel->setObjectName(QStringLiteral("LF_statusLabel"));
    QFont statusFont = statusLabel->font();
    statusFont.setBold(true);
    statusLabel->setFont(statusFont);
    statusRow->addWidget(statusLabel, 1);
    protocolLabel = new QLabel(QString(), resultBox);
    formatLabel = new QLabel(QString(), resultBox);
    bitsLabel = new QLabel(QString(), resultBox);
    confidenceLabel = new QLabel(QString(), resultBox);
    statusRow->addWidget(protocolLabel);
    statusRow->addWidget(formatLabel);
    statusRow->addWidget(bitsLabel);
    statusRow->addWidget(confidenceLabel);
    resultLayout->addLayout(statusRow);

    noteLabel = new QLabel(QString(), resultBox);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    noteLabel->setVisible(false);
    resultLayout->addWidget(noteLabel);

    fieldTable = new QTableWidget(0, 2, resultBox);
    fieldTable->setObjectName(QStringLiteral("LF_fieldTable"));
    fieldTable->setAccessibleName(tr("Parsed fields"));
    fieldTable->setHorizontalHeaderLabels(QStringList() << tr("Field") << tr("Value"));
    fieldTable->horizontalHeader()->setStretchLastSection(true);
    fieldTable->verticalHeader()->setVisible(false);
    fieldTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    fieldTable->setAlternatingRowColors(true);
    resultLayout->addWidget(fieldTable, 1);

    // ---------------- details ----------------
    detailTabs = new QTabWidget(splitter);
    rawView = new QPlainTextEdit(detailTabs);
    rawView->setReadOnly(true);
    rawView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detailTabs->addTab(rawView, tr("Raw output"));

    diagnosticsView = new QPlainTextEdit(detailTabs);
    diagnosticsView->setReadOnly(true);
    detailTabs->addTab(diagnosticsView, tr("Warnings and unparsed fields"));

    t55xxView = new QPlainTextEdit(detailTabs);
    t55xxView->setReadOnly(true);
    detailTabs->addTab(t55xxView, tr("T55xx (read-only)"));

    splitter->addWidget(inputBox);
    splitter->addWidget(resultBox);
    splitter->addWidget(detailTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);

    clearResult();
}

void LfParserTab::onInputChanged()
{
    setSource(tr("manual input"));
}

void LfParserTab::setSource(const QString &text)
{
    source = text;
    sourceLabel->setText(tr("Source: %1").arg(text));
}

void LfParserTab::setClientText(const QString &text)
{
    input->setPlainText(text);
    setSource(tr("RawCommand output"));
    parseNow();
}

void LfParserTab::requestClientText()
{
    emit clientTextRequested();
}

void LfParserTab::clearInput()
{
    input->clear();
    setSource(tr("manual input"));
    clearResult();
}

void LfParserTab::importTextFile()
{
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Import LF output text"), QString(),
                tr("Text files (*.txt *.log *.md);;All files (*)"));
    if(path.isEmpty())
        return;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        statusLabel->setText(tr("Cannot open the file."));
        return;
    }
    const QString text = QString::fromLocal8Bit(file.readAll());
    file.close();
    input->setPlainText(text);
    setSource(tr("text file"));
    parseNow();
}

void LfParserTab::clearResult()
{
    statusLabel->setText(tr("No input parsed yet."));
    protocolLabel->clear();
    formatLabel->clear();
    bitsLabel->clear();
    confidenceLabel->clear();
    noteLabel->clear();
    noteLabel->setVisible(false);
    fieldTable->setRowCount(0);
    if(rawView)
        rawView->clear();
    if(diagnosticsView)
        diagnosticsView->clear();
    if(t55xxView)
        t55xxView->clear();
}

QString LfParserTab::optionalInt(const Lf::Optional<int> &value)
{
    return value.isPresent() ? QString::number(value.value()) : tr("Not provided");
}

QString LfParserTab::optionalUInt32(const Lf::Optional<quint32> &value)
{
    return value.isPresent() ? QString::number(value.value()) : tr("Not provided");
}

QString LfParserTab::optionalUInt64(const Lf::Optional<quint64> &value)
{
    return value.isPresent() ? QString::number(value.value()) : tr("Not provided");
}

QString LfParserTab::extraLabel(const QString &key)
{
    if(key == QLatin1String("countryCode")) return tr("Country code");
    if(key == QLatin1String("animalBit")) return tr("Animal bit");
    if(key == QLatin1String("extendedData")) return tr("Extended data");
    if(key == QLatin1String("extendedValue")) return tr("Extended value");
    if(key == QLatin1String("crc")) return tr("CRC");
    if(key == QLatin1String("crcState")) return tr("CRC state");
    if(key == QLatin1String("rudiBit")) return tr("RUDI bit");
    if(key == QLatin1String("reserved")) return tr("Reserved");
    if(key == QLatin1String("uniqueTagId")) return tr("Unique tag ID");
    if(key == QLatin1String("rfDivisor")) return tr("RF divisor");
    if(key == QLatin1String("facilityCodeDecimal")) return tr("Facility code (decimal)");
    if(key == QLatin1String("facilityCodeHex")) return tr("Facility code (hex)");
    if(key == QLatin1String("cardHex")) return tr("Card (hex)");
    if(key == QLatin1String("cardIdHex")) return tr("Card ID (hex)");
    if(key == QLatin1String("regionCode")) return tr("Region code");
    if(key == QLatin1String("oemCode")) return tr("OEM code");
    if(key == QLatin1String("customerCode")) return tr("Customer code");
    if(key == QLatin1String("customerCodeHex")) return tr("Customer code (hex)");
    if(key == QLatin1String("issueLevel")) return tr("Issue level");
    if(key == QLatin1String("keriType")) return tr("Keri type");
    if(key == QLatin1String("wiegandFormat")) return tr("Wiegand format");
    if(key == QLatin1String("wiegand")) return tr("Wiegand");
    if(key == QLatin1String("parity")) return tr("Parity");
    if(key == QLatin1String("checksum")) return tr("Checksum");
    if(key == QLatin1String("checksumState")) return tr("Checksum state");
    if(key == QLatin1String("rawId")) return tr("Raw ID");
    if(key == QLatin1String("rawPayload")) return tr("Raw payload");
    if(key == QLatin1String("fullCode")) return tr("Full code");
    if(key == QLatin1String("frame")) return tr("Frame");
    if(key == QLatin1String("frameData")) return tr("Frame data");
    if(key == QLatin1String("subtype")) return tr("Subtype");
    if(key == QLatin1String("variant")) return tr("Variant");
    if(key == QLatin1String("version")) return tr("Version");
    if(key == QLatin1String("year")) return tr("Year");
    if(key == QLatin1String("mode")) return tr("Mode");
    if(key == QLatin1String("dez8")) return tr("Decimal (8 digits)");
    if(key == QLatin1String("dez14")) return tr("Decimal (14 digits)");
    if(key == QLatin1String("dez15")) return tr("Decimal (15 digits)");
    if(key == QLatin1String("cansec37Fc")) return tr("Cansec 37 facility code");
    if(key == QLatin1String("cansec37Cn")) return tr("Cansec 37 card number");
    if(key == QLatin1String("keyscan40010Fc")) return tr("Keyscan 40010 facility code");
    if(key == QLatin1String("keyscan40010Cn")) return tr("Keyscan 40010 card number");
    if(key == QLatin1String("patternCdviFc")) return tr("CDVI pattern facility code");
    if(key == QLatin1String("patternCdviCn")) return tr("CDVI pattern card number");
    if(key == QLatin1String("rbhCn")) return tr("RBH card number");
    if(key == QLatin1String("hcc40Id")) return tr("HCC40 ID");
    if(key == QLatin1String("pacCardHex")) return tr("PAC card (hex)");
    if(key == QLatin1String("id88Hex")) return tr("ID88 (hex)");
    if(key == QLatin1String("internalId")) return tr("Internal ID");
    if(key == QLatin1String("displayed")) return tr("Displayed value");
    if(key == QLatin1String("printed")) return tr("Printed value");
    if(key == QLatin1String("printedHex")) return tr("Printed value (hex)");
    if(key == QLatin1String("secondCard")) return tr("Second card");
    if(key == QLatin1String("fingerprint")) return tr("Fingerprint");
    if(key == QLatin1String("electra")) return tr("Electra");
    if(key == QLatin1String("heden2l")) return tr("Heden 2L");
    if(key == QLatin1String("xlHigh")) return tr("XL high bits");
    if(key == QLatin1String("xlLow")) return tr("XL low bits");
    if(key == QLatin1String("xor")) return tr("XOR");
    return key;
}

void LfParserTab::parseNow()
{
    const QString text = input->toPlainText();
    if(text.trimmed().isEmpty())
    {
        clearResult();
        statusLabel->setText(tr("Input is empty."));
        noteLabel->setText(tr("Paste Proxmark3 LF output or import a text file."));
        noteLabel->setVisible(true);
        return;
    }

    const LFParseResult result = LFParserRegistry::instance().parse(text);

    fillResult(result);

    rawView->setPlainText(text);
    QStringList diagnostics;
    if(!result.warnings.isEmpty())
    {
        diagnostics << tr("Warnings:");
        for(const QString &warning : result.warnings)
            diagnostics << QStringLiteral("  - ") + warning;
    }
    if(!result.unparsedFields.isEmpty())
    {
        diagnostics << tr("Unparsed fields:");
        for(const QString &field : result.unparsedFields)
            diagnostics << QStringLiteral("  - ") + field;
    }
    if(!result.formatCandidates.isEmpty())
    {
        diagnostics << tr("Format candidates: %1").arg(result.formatCandidates.join(QStringLiteral(", ")));
    }
    if(diagnostics.isEmpty())
        diagnostics << tr("No warnings.");
    diagnosticsView->setPlainText(diagnostics.join(QLatin1Char('\n')));

    fillT55xx();
}

void LfParserTab::fillResult(const LFParseResult &result)
{
    switch(result.status)
    {
    case Lf::Status::Identified:
        statusLabel->setText(tr("Identified"));
        break;
    case Lf::Status::Partial:
        statusLabel->setText(tr("Partial parse"));
        break;
    case Lf::Status::Unidentified:
        statusLabel->setText(tr("No supported protocol found"));
        break;
    case Lf::Status::InvalidInput:
        statusLabel->setText(tr("Invalid input"));
        break;
    }

    const bool hasProtocol = result.protocol != Lf::Protocol::Unknown;
    protocolLabel->setText(hasProtocol ? Lf::protocolName(result.protocol) : QString());
    formatLabel->setText(result.format.isEmpty()
                         ? QString()
                         : tr("Format: %1").arg(result.format));
    bitsLabel->setText(result.bitLength.isPresent()
                       ? tr("%1 bit").arg(result.bitLength.value())
                       : QString());
    confidenceLabel->setText(result.status == Lf::Status::Unidentified
                             ? QString()
                             : tr("Confidence: %1").arg(Lf::confidenceName(result.confidence)));

    QStringList notes;
    if(!result.formatCandidates.isEmpty())
        notes << tr("Ambiguous format: %1 (the parser deliberately does not pick one).")
                 .arg(result.formatCandidates.join(QStringLiteral(" / ")));
    if(result.status == Lf::Status::Partial)
        notes << tr("Protocol recognised, but required fields are incomplete.");
    if(result.status == Lf::Status::Unidentified)
        notes << tr("No supported LF protocol signature was found in this text.");
    const QString lower = result.sourceText.toLower();
    if(lower.contains(QStringLiteral("unknown command"))
            || lower.contains(QStringLiteral("not supported"))
            || lower.contains(QStringLiteral("no response")))
        notes << tr("The client reports an unsupported or missing command: the connected "
                    "firmware may not include LF support. Text parsing still works.");
    noteLabel->setText(notes.join(QLatin1Char('\n')));
    noteLabel->setVisible(!notes.isEmpty());

    fieldTable->setRowCount(0);
    auto addRow = [this](const QString &name, const QString &value)
    {
        const int row = fieldTable->rowCount();
        fieldTable->insertRow(row);
        fieldTable->setItem(row, 0, new QTableWidgetItem(name));
        fieldTable->setItem(row, 1, new QTableWidgetItem(value));
    };

    if(hasProtocol || result.status == Lf::Status::Partial)
    {
        addRow(tr("Protocol"), valueOrMissing(hasProtocol ? Lf::protocolName(result.protocol) : QString()));
        addRow(tr("Format"), valueOrMissing(result.format));
        addRow(tr("Bit length"), optionalInt(result.bitLength));
        addRow(tr("Facility code"), optionalUInt32(result.facilityCode));
        addRow(tr("Card number"), optionalUInt64(result.cardNumber));
        addRow(tr("Hex ID"), valueOrMissing(result.hexId));
        addRow(tr("Decimal ID"), optionalUInt64(result.decimalId));
        addRow(tr("Raw data"), valueOrMissing(result.rawData));
        for(const LfExtraField &field : result.extraFields)
            addRow(extraLabel(field.name), valueOrMissing(field.value));
    }
    fieldTable->resizeColumnsToContents();
}

void LfParserTab::fillT55xx()
{
    const QString text = input->toPlainText();
    LfT55xx::DetectInfo info = LfT55xx::parseInfo(text);
    if(!info.detected)
        info = LfT55xx::parseDetect(text);

    QStringList lines;
    if(!info.detected && info.block0Hex.isEmpty())
    {
        lines << tr("No T55xx detect/info data was found in this text.");
        lines << tr("T55xx parsing is read-only: this page never writes, locks or "
                    "erases a tag.");
        t55xxView->setPlainText(lines.join(QLatin1Char('\n')));
        return;
    }

    lines << tr("Detected: %1").arg(info.detected ? tr("yes") : tr("no"));
    if(!info.chipType.isEmpty())
        lines << tr("Chip: %1").arg(info.chipType);
    if(info.passwordSet.isPresent())
        lines << tr("Password set: %1").arg(info.passwordSet.value() ? tr("yes") : tr("no"));
    if(!info.block0Hex.isEmpty())
        lines << tr("Block 0: %1").arg(info.block0Hex);
    if(!info.modulationText.isEmpty())
        lines << tr("Modulation: %1").arg(info.modulationText);
    if(info.bitRate.isPresent())
        lines << tr("Bit rate: %1").arg(info.bitRate.value());
    if(info.offset.isPresent())
        lines << tr("Offset: %1").arg(info.offset.value());
    if(info.inverted.isPresent())
        lines << tr("Inverted: %1").arg(info.inverted.value() ? tr("yes") : tr("no"));
    for(const LfExtraField &field : info.extraFields)
        lines << QStringLiteral("%1: %2").arg(extraLabel(field.name), field.value);

    const LfT55xx::Block0 block = info.block0Hex.isEmpty()
            ? LfT55xx::Block0()
            : LfT55xx::parseBlock0(info.block0Hex);
    if(block.valid)
    {
        lines << QString();
        lines << tr("Block 0 decoded (read-only):");
        if(block.modulation.isPresent())
            lines << tr("  Modulation: %1").arg(LfT55xx::modulationName(block.modulation.value()));
        lines << tr("  Extended mode: %1").arg(block.extend ? tr("yes") : tr("no"));
        if(block.bitRateIndex.isPresent())
            lines << tr("  Bit rate index: %1").arg(block.bitRateIndex.value());
        if(block.bitRateDivisor.isPresent())
            lines << tr("  RF divisor: RF/%1").arg(block.bitRateDivisor.value());
        if(block.maxBlock.isPresent())
            lines << tr("  Max block: %1").arg(block.maxBlock.value());
        if(block.masterKey.isPresent())
            lines << tr("  Master key: %1").arg(block.masterKey.value());
        for(const LfExtraField &field : block.fields)
            lines << QStringLiteral("  %1: %2").arg(extraLabel(field.name), field.value);
        for(const QString &warning : block.warnings)
            lines << tr("  Warning: %1").arg(warning);
    }

    if(!info.warnings.isEmpty())
    {
        lines << QString();
        lines << tr("Warnings:");
        for(const QString &warning : info.warnings)
            lines << QStringLiteral("  - ") + warning;
    }
    lines << QString();
    lines << tr("This page is read-only for T55xx: no write, lock, OTP or password "
                "operation is offered.");

    t55xxView->setPlainText(lines.join(QLatin1Char('\n')));
}
