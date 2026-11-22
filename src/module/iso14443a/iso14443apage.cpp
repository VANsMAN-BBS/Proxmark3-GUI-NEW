#include "iso14443apage.h"

#include "iso14443aservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTime>
#include <QVBoxLayout>

namespace
{

QTableWidgetItem *readOnlyItem(const QString &text, const QString &tooltip)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (!tooltip.isEmpty())
        item->setToolTip(tooltip);
    return item;
}

QString na()
{
    return Iso14443aPage::tr("N/A");
}

} // namespace

Iso14443aPage::Iso14443aPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Iso14443a_page"));
    buildUi();

    m_service = new Iso14443aService(this);
    connect(m_service, &Iso14443aService::ready, this, &Iso14443aPage::onReady);
    connect(m_service, &Iso14443aService::requestStarted,
            this, &Iso14443aPage::onRequestStarted);
    connect(m_service, &Iso14443aService::requestFinished,
            this, &Iso14443aPage::onRequestFinished);
    connect(m_service, &Iso14443aService::requestFailed,
            this, &Iso14443aPage::onRequestFailed);
    connect(m_service, &Iso14443aService::logLine,
            this, &Iso14443aPage::onServiceLog);

    resetResult();
    onServiceLog(tr("Ready. Open a PM3 trace or a raw capture to begin."));
}

Iso14443aPage::~Iso14443aPage()
{
    shutdown();
}

QString Iso14443aPage::pageTitle()
{
    return tr("ISO14443A Parser");
}

void Iso14443aPage::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *toolbar = new QHBoxLayout();
    m_openTraceButton = new QPushButton(tr("Open PM3 Trace…"), this);
    m_openTraceButton->setObjectName(QStringLiteral("Iso14443a_openTraceButton"));
    m_openCaptureButton = new QPushButton(tr("Open Raw Capture…"), this);
    m_openCaptureButton->setObjectName(QStringLiteral("Iso14443a_openCaptureButton"));
    m_importTraceButton = new QPushButton(tr("Import Latest Terminal Trace"), this);
    m_importTraceButton->setObjectName(QStringLiteral("Iso14443a_importTraceButton"));
    m_parseButton = new QPushButton(tr("Parse"), this);
    m_parseButton->setObjectName(QStringLiteral("Iso14443a_parseButton"));
    m_clearButton = new QPushButton(tr("Clear"), this);
    m_clearButton->setObjectName(QStringLiteral("Iso14443a_clearButton"));
    m_exportButton = new QPushButton(tr("Export JSON…"), this);
    m_exportButton->setObjectName(QStringLiteral("Iso14443a_exportButton"));

    toolbar->addWidget(m_openTraceButton);
    toolbar->addWidget(m_openCaptureButton);
    toolbar->addWidget(m_importTraceButton);
    toolbar->addSpacing(10);
    toolbar->addWidget(m_parseButton);
    toolbar->addWidget(m_clearButton);
    toolbar->addWidget(m_exportButton);
    toolbar->addStretch();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("Iso14443a_statusLabel"));
    m_statusLabel->setWordWrap(true);
    toolbar->addWidget(m_statusLabel, 1);
    root->addLayout(toolbar);

    QGroupBox *overviewGroup = new QGroupBox(tr("Overview"), this);
    QFormLayout *overview = new QFormLayout(overviewGroup);
    overview->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QHBoxLayout *row1 = new QHBoxLayout();
    m_uidValue = new QLabel(na(), this);
    m_uidValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_atqaValue = new QLabel(na(), this);
    m_atqaValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sakValue = new QLabel(na(), this);
    m_sakValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row1->addWidget(new QLabel(tr("UID:"), this));
    row1->addWidget(m_uidValue, 1);
    row1->addWidget(new QLabel(tr("ATQA:"), this));
    row1->addWidget(m_atqaValue);
    row1->addWidget(new QLabel(tr("SAK:"), this));
    row1->addWidget(m_sakValue);
    row1->addStretch();
    overview->addRow(row1);

    QHBoxLayout *row2 = new QHBoxLayout();
    m_uidLenValue = new QLabel(na(), this);
    m_cardTypeValue = new QLabel(na(), this);
    m_cardTypeValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cardTypeValue->setWordWrap(true);
    m_atsValue = new QLabel(na(), this);
    m_atsValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_atsValue->setWordWrap(true);
    row2->addWidget(new QLabel(tr("UID length:"), this));
    row2->addWidget(m_uidLenValue);
    row2->addSpacing(16);
    row2->addWidget(new QLabel(tr("Card type:"), this));
    row2->addWidget(m_cardTypeValue, 1);
    row2->addWidget(new QLabel(tr("ATS:"), this));
    row2->addWidget(m_atsValue, 1);
    overview->addRow(row2);

    QHBoxLayout *row3 = new QHBoxLayout();
    m_stateValue = new QLabel(na(), this);
    m_stateValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_crcValue = new QLabel(na(), this);
    m_parityValue = new QLabel(na(), this);
    row3->addWidget(new QLabel(tr("State:"), this));
    row3->addWidget(m_stateValue, 1);
    row3->addWidget(new QLabel(tr("CRC16-A:"), this));
    row3->addWidget(m_crcValue);
    row3->addWidget(new QLabel(tr("Parity:"), this));
    row3->addWidget(m_parityValue);
    row3->addStretch();
    overview->addRow(row3);
    root->addWidget(overviewGroup);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("Iso14443a_frameTable"));
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(QStringList()
        << tr("Time") << tr("Direction") << tr("Type") << tr("Raw")
        << tr("CRC") << tr("Parity") << tr("Result"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 120);
    m_table->setColumnWidth(1, 120);
    m_table->setColumnWidth(2, 120);
    m_table->setColumnWidth(3, 220);
    m_table->setColumnWidth(4, 80);
    m_table->setColumnWidth(5, 80);
    connect(m_table, &QTableWidget::currentCellChanged,
            this, &Iso14443aPage::onSelectionChanged);
    root->addWidget(m_table, 3);

    QGroupBox *detailsGroup = new QGroupBox(tr("Frame details"), this);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsGroup);
    m_details = new QTextBrowser(detailsGroup);
    m_details->setObjectName(QStringLiteral("Iso14443a_details"));
    m_details->setOpenExternalLinks(false);
    m_details->setPlaceholderText(tr("Select a frame to see its raw bytes and decode."));
    detailsLayout->addWidget(m_details);
    root->addWidget(detailsGroup, 2);

    QGroupBox *logGroup = new QGroupBox(tr("Parser log"), this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QPlainTextEdit(logGroup);
    m_logEdit->setObjectName(QStringLiteral("Iso14443a_serviceLog"));
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(500);
    m_logEdit->setMaximumHeight(110);
    logLayout->addWidget(m_logEdit);
    root->addWidget(logGroup);

    connect(m_openTraceButton, &QPushButton::clicked, this, &Iso14443aPage::openTrace);
    connect(m_openCaptureButton, &QPushButton::clicked, this, &Iso14443aPage::openCapture);
    connect(m_importTraceButton, &QPushButton::clicked, this, &Iso14443aPage::importLatestTrace);
    connect(m_parseButton, &QPushButton::clicked, this, &Iso14443aPage::parseCurrent);
    connect(m_clearButton, &QPushButton::clicked, this, &Iso14443aPage::clearView);
    connect(m_exportButton, &QPushButton::clicked, this, &Iso14443aPage::exportJson);

    updateButtons();
}

void Iso14443aPage::setClientWorkingDirectory(const QString &directory)
{
    m_clientWorkingDir = directory;
}

void Iso14443aPage::setTraceDirectoryProvider(const std::function<QStringList()> &provider)
{
    m_traceProvider = provider;
}

void Iso14443aPage::setTerminalText(const QString &text)
{
    m_terminalText = text;
}

bool Iso14443aPage::isBusy() const
{
    return m_busy;
}

void Iso14443aPage::shutdown()
{
    if (m_service)
        m_service->shutdown();
}

QString Iso14443aPage::initialDirectory() const
{
    if (!m_sourcePath.isEmpty())
    {
        const QFileInfo info(m_sourcePath);
        if (info.exists())
            return info.absolutePath();
    }
    if (!m_clientWorkingDir.isEmpty() && QDir(m_clientWorkingDir).exists())
        return m_clientWorkingDir;
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

void Iso14443aPage::openTrace()
{
    if (m_busy)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open PM3 trace"), initialDirectory(),
        tr("PM3 trace files (*.trace *.trc);;All files (*)"));
    if (path.isEmpty())
        return;
    m_sourceKind = TraceSource;
    m_sourcePath = path;
    startParse();
}

void Iso14443aPage::openCapture()
{
    if (m_busy)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open raw capture"), initialDirectory(),
        tr("Capture files (*.txt *.log *.hex);;All files (*)"));
    if (path.isEmpty())
        return;
    m_sourceKind = CaptureSource;
    m_sourcePath = path;
    startParse();
}

void Iso14443aPage::parseCurrent()
{
    if (m_busy)
        return;
    if (m_sourceKind == NoSource || m_sourcePath.isEmpty())
    {
        QMessageBox::information(this, tr("Nothing to parse"),
                                 tr("Open a PM3 trace or a raw capture first."));
        return;
    }
    startParse();
}

void Iso14443aPage::startParse()
{
    if (m_busy || !m_service)
        return;
    if (m_sourceKind == NoSource || m_sourcePath.isEmpty())
        return;

    resetResult();
    m_activeAction = m_sourceKind == TraceSource
        ? QStringLiteral("parse_trace")
        : QStringLiteral("parse_capture");
    m_activeRequest = 0;
    m_statusLabel->setText(tr("Parsing %1…").arg(QFileInfo(m_sourcePath).fileName()));
    setBusy(true);

    if (m_sourceKind == TraceSource)
        m_activeRequest = m_service->parseTrace(m_sourcePath);
    else
        m_activeRequest = m_service->parseCapture(m_sourcePath);
}

void Iso14443aPage::clearView()
{
    if (m_busy)
        return;
    m_sourceKind = NoSource;
    m_sourcePath.clear();
    resetResult();
    m_statusLabel->setText(tr("Cleared."));
    onServiceLog(tr("Result cleared."));
}

void Iso14443aPage::resetResult()
{
    m_lastData = QJsonObject();
    m_frames.clear();
    m_table->setRowCount(0);
    m_details->clear();
    m_uidValue->setText(na());
    m_atqaValue->setText(na());
    m_sakValue->setText(na());
    m_uidLenValue->setText(na());
    m_cardTypeValue->setText(na());
    m_atsValue->setText(na());
    m_stateValue->setText(na());
    m_crcValue->setText(na());
    m_parityValue->setText(na());
    updateButtons();
}

void Iso14443aPage::setBusy(bool busy)
{
    m_busy = busy;
    updateButtons();
}

void Iso14443aPage::updateButtons()
{
    const bool hasResult = !m_lastData.isEmpty();
    if (m_openTraceButton)
        m_openTraceButton->setEnabled(!m_busy);
    if (m_openCaptureButton)
        m_openCaptureButton->setEnabled(!m_busy);
    if (m_importTraceButton)
        m_importTraceButton->setEnabled(!m_busy);
    if (m_parseButton)
        m_parseButton->setEnabled(!m_busy && m_sourceKind != NoSource);
    if (m_clearButton)
        m_clearButton->setEnabled(!m_busy && (hasResult || m_sourceKind != NoSource));
    if (m_exportButton)
        m_exportButton->setEnabled(!m_busy && hasResult);
}

void Iso14443aPage::onRequestStarted(quint64 requestId, const QString &action)
{
    if (!action.startsWith(QLatin1String("parse")))
        return;
    m_activeRequest = requestId;
    m_activeAction = action;
    setBusy(true);
}

void Iso14443aPage::onRequestFinished(quint64 requestId, const QString &action,
                                      const QJsonObject &data)
{
    if (!action.startsWith(QLatin1String("parse")))
        return;
    if (m_activeRequest != 0 && requestId != m_activeRequest)
        return;

    m_activeRequest = 0;
    m_activeAction.clear();
    setBusy(false);
    showData(data);
}

void Iso14443aPage::onRequestFailed(quint64 requestId, const QString &action,
                                    const QString &code, const QString &message)
{
    if (!action.startsWith(QLatin1String("parse")))
        return;
    if (m_activeRequest != 0 && requestId != m_activeRequest)
        return;

    m_activeRequest = 0;
    m_activeAction.clear();
    setBusy(false);
    const QString text = tr("Parse failed (%1): %2").arg(code, message);
    m_statusLabel->setText(text);
    onServiceLog(text);
}

void Iso14443aPage::onReady()
{
    if (!m_busy)
        m_statusLabel->setText(tr("Parser ready."));
}

void Iso14443aPage::onServiceLog(const QString &line)
{
    if (!m_logEdit || line.isEmpty())
        return;
    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, line));
}

void Iso14443aPage::showData(const QJsonObject &data)
{
    m_lastData = data;
    m_frames.clear();
    const QJsonArray frames = data.value(QStringLiteral("frames")).toArray();
    m_frames.reserve(frames.size());
    for (const QJsonValue &value : frames)
    {
        if (value.isObject())
            m_frames.append(value.toObject());
    }

    populateTable();
    updateOverview();
    updateButtons();

    const QJsonObject validation = data.value(QStringLiteral("validation")).toObject();
    const int total = validation.value(QStringLiteral("frames_total")).toInt();
    const int readErrors = validation.value(QStringLiteral("read_errors")).toInt();
    if (readErrors > 0)
        m_statusLabel->setText(tr("Parsed %1 frame(s); %2 input problem(s).").arg(total).arg(readErrors));
    else
        m_statusLabel->setText(tr("Parsed %1 frame(s).").arg(total));

    if (!m_frames.isEmpty())
    {
        m_table->setCurrentCell(0, 0);
        updateDetails(0);
    }
    else
    {
        m_details->setHtml(QStringLiteral("<p>%1</p>").arg(htmlEscape(tr("No frames were found."))));
    }
}

void Iso14443aPage::populateTable()
{
    m_table->setRowCount(0);
    m_table->setRowCount(m_frames.size());

    for (int row = 0; row < m_frames.size(); ++row)
    {
        const QJsonObject frame = m_frames.at(row);
        const QString timestamp = frame.value(QStringLiteral("timestamp_hex")).toString();
        m_table->setItem(row, 0, readOnlyItem(timestamp.isEmpty() ? QStringLiteral("—") : timestamp,
                                              timestamp));

        const QString direction = directionLabel(frame.value(QStringLiteral("direction")).toString());
        m_table->setItem(row, 1, readOnlyItem(direction, direction));

        const QString type = typeLabel(frame.value(QStringLiteral("type")).toString());
        m_table->setItem(row, 2, readOnlyItem(type, type));

        const QString raw = frame.value(QStringLiteral("raw")).toString();
        m_table->setItem(row, 3, readOnlyItem(elideText(raw, 48), raw));

        const QJsonObject crc = frame.value(QStringLiteral("crc")).toObject();
        const QJsonObject parity = frame.value(QStringLiteral("parity")).toObject();
        m_table->setItem(row, 4, readOnlyItem(
            statusText(crc.value(QStringLiteral("checked")).toBool(),
                       crc.value(QStringLiteral("ok")).toBool()),
            statusText(crc.value(QStringLiteral("checked")).toBool(),
                       crc.value(QStringLiteral("ok")).toBool())));
        m_table->setItem(row, 5, readOnlyItem(
            statusText(parity.value(QStringLiteral("checked")).toBool(),
                       parity.value(QStringLiteral("ok")).toBool()),
            statusText(parity.value(QStringLiteral("checked")).toBool(),
                       parity.value(QStringLiteral("ok")).toBool())));

        QString result = frame.value(QStringLiteral("summary")).toString();
        if (frame.value(QStringLiteral("ciphertext")).toBool())
            result = tr("[Ciphertext]") + QLatin1Char(' ') + result;
        if (result.isEmpty())
            result = type;
        m_table->setItem(row, 6, readOnlyItem(result, result));
    }
}

void Iso14443aPage::updateOverview()
{
    const QJsonObject card = m_lastData.value(QStringLiteral("card_info")).toObject();
    const QJsonObject validation = m_lastData.value(QStringLiteral("validation")).toObject();

    const QString uid = card.value(QStringLiteral("uid")).toString();
    m_uidValue->setText(uid.isEmpty() ? na() : uid);

    const QString atqa = card.value(QStringLiteral("atqa_hex")).toString();
    m_atqaValue->setText(atqa.isEmpty() ? na() : QStringLiteral("0x%1").arg(atqa));

    const QString sak = card.value(QStringLiteral("sak_hex")).toString();
    m_sakValue->setText(sak.isEmpty() ? na() : QStringLiteral("0x%1").arg(sak));

    const int uidLen = card.value(QStringLiteral("uid_len")).toInt(0);
    m_uidLenValue->setText(uidLen > 0 ? tr("%1 bytes").arg(uidLen) : na());

    const QString cardType = card.value(QStringLiteral("card_type")).toString();
    m_cardTypeValue->setText(cardType.isEmpty() ? na() : cardType);

    const QJsonValue atsValue = card.value(QStringLiteral("ats"));
    if (atsValue.isObject())
    {
        const QJsonObject ats = atsValue.toObject();
        QString text = ats.value(QStringLiteral("hex")).toString();
        const int tl = ats.value(QStringLiteral("tl")).toInt(0);
        const int fsci = ats.value(QStringLiteral("fsci")).toInt(0);
        if (!text.isEmpty())
            text += QLatin1Char(' ') + tr("(TL=%1, FSCI=%2)").arg(tl).arg(fsci);
        m_atsValue->setText(text.isEmpty() ? na() : text);
    }
    else
    {
        m_atsValue->setText(na());
    }

    m_stateValue->setText(phaseLabel(card.value(QStringLiteral("phase")).toString()));

    const int crcChecked = validation.value(QStringLiteral("crc_checked")).toInt(0);
    const int crcOk = validation.value(QStringLiteral("crc_ok")).toInt(0);
    const int crcCipher = validation.value(QStringLiteral("crc_failed_ciphertext")).toInt(0);
    if (crcChecked <= 0)
    {
        m_crcValue->setText(na());
    }
    else if (crcCipher > 0 && crcOk == crcChecked - crcCipher)
    {
        m_crcValue->setText(tr("OK %1/%2 (ciphertext %3)").arg(crcOk).arg(crcChecked).arg(crcCipher));
    }
    else
    {
        m_crcValue->setText(crcOk == crcChecked
            ? tr("OK %1/%2").arg(crcOk).arg(crcChecked)
            : tr("Fail %1/%2").arg(crcOk).arg(crcChecked));
    }

    const int parityChecked = validation.value(QStringLiteral("parity_checked")).toInt(0);
    const int parityOk = validation.value(QStringLiteral("parity_ok")).toInt(0);
    const int parityCipher = validation.value(QStringLiteral("parity_failed_ciphertext")).toInt(0);
    if (parityChecked <= 0)
    {
        m_parityValue->setText(na());
    }
    else if (parityCipher > 0 && parityOk == parityChecked - parityCipher)
    {
        m_parityValue->setText(tr("OK %1/%2 (ciphertext %3)").arg(parityOk).arg(parityChecked).arg(parityCipher));
    }
    else
    {
        m_parityValue->setText(parityOk == parityChecked
            ? tr("OK %1/%2").arg(parityOk).arg(parityChecked)
            : tr("Fail %1/%2").arg(parityOk).arg(parityChecked));
    }
}

void Iso14443aPage::onSelectionChanged()
{
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_frames.size())
        updateDetails(row);
}

void Iso14443aPage::updateDetails(int row)
{
    if (row < 0 || row >= m_frames.size())
        return;

    const QJsonObject frame = m_frames.at(row);
    const QString raw = frame.value(QStringLiteral("raw")).toString();
    const QString data = frame.value(QStringLiteral("data")).toString();
    const int rawLen = frame.value(QStringLiteral("raw_len")).toInt(0);
    const int dataLen = frame.value(QStringLiteral("data_len")).toInt(0);
    const int bitCount = frame.value(QStringLiteral("bit_count")).toInt(0);
    const int validBits = frame.value(QStringLiteral("valid_bits")).toInt(0);
    const QJsonObject crc = frame.value(QStringLiteral("crc")).toObject();
    const QJsonObject parity = frame.value(QStringLiteral("parity")).toObject();
    const QJsonObject decode = frame.value(QStringLiteral("decode")).toObject();
    const QJsonObject error = frame.value(QStringLiteral("error")).toObject();
    const QJsonValue atsValue = decode.value(QStringLiteral("ats"));

    QStringList html;
    html << QStringLiteral("<html><head><style>"
                           "body{font-family:'Segoe UI',sans-serif;font-size:12px;}"
                           "table{border-collapse:collapse;}"
                           "td{padding:2px 8px 2px 0;vertical-align:top;}"
                           "td.k{color:#666;white-space:nowrap;}"
                           "code,pre{font-family:Consolas,'Courier New',monospace;}"
                           "pre{white-space:pre-wrap;word-break:break-all;}"
                           "</style></head><body>");

    html << QStringLiteral("<h3>%1 %2</h3>")
        .arg(htmlEscape(tr("Frame")),
             htmlEscape(QStringLiteral("#%1").arg(frame.value(QStringLiteral("seq")).toInt(row + 1))));

    if (frame.value(QStringLiteral("ciphertext")).toBool())
    {
        QString reason = frame.value(QStringLiteral("ciphertext_reason")).toString();
        if (reason.isEmpty())
            reason = tr("The parser marked this frame as Crypto1 ciphertext.");
        html << QStringLiteral("<div style=\"border:1px solid #b00020;background:#fff0f0;"
                              "color:#b00020;font-weight:bold;padding:6px;margin:6px 0;\">"
                              "%1<br><span style=\"font-weight:normal;\">%2</span></div>")
            .arg(htmlEscape(tr("Ciphertext (Crypto1) — values are not plaintext.")),
                 htmlEscape(reason));
    }

    html << QStringLiteral("<table>");
    const QString timestamp = frame.value(QStringLiteral("timestamp_hex")).toString();
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
        .arg(htmlEscape(tr("Timestamp")), htmlEscape(timestamp.isEmpty() ? QStringLiteral("—") : timestamp));

    const QJsonValue delta = frame.value(QStringLiteral("delta"));
    if (!delta.isNull() && !delta.isUndefined())
    {
        QString deltaText = QString::number(delta.toInt());
        const QJsonValue deltaUs = frame.value(QStringLiteral("delta_us"));
        if (!deltaUs.isNull() && !deltaUs.isUndefined())
            deltaText += tr(" cycles (%1 µs)").arg(deltaUs.toInt());
        else
            deltaText += tr(" (24-bit units)");
        html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
            .arg(htmlEscape(tr("Delta")), htmlEscape(deltaText));
    }

    html << QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
        .arg(htmlEscape(tr("Direction")),
             htmlEscape(directionLabel(frame.value(QStringLiteral("direction")).toString())));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
        .arg(htmlEscape(tr("Type")),
             htmlEscape(typeLabel(frame.value(QStringLiteral("type")).toString())));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
        .arg(htmlEscape(tr("State")),
             htmlEscape(phaseLabel(frame.value(QStringLiteral("phase")).toString())));

    html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
        .arg(htmlEscape(tr("Raw payload (wire)")), htmlEscape(raw.isEmpty() ? QStringLiteral("—") : raw));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
        .arg(htmlEscape(tr("Raw ASCII")), htmlEscape(asciiOf(raw)));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
        .arg(htmlEscape(tr("Decoded data")), htmlEscape(data.isEmpty() ? QStringLiteral("—") : data));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
        .arg(htmlEscape(tr("Data ASCII")), htmlEscape(asciiOf(data)));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
        .arg(htmlEscape(tr("Length")),
             htmlEscape(tr("%1 wire byte(s), %2 data byte(s)").arg(rawLen).arg(dataLen)));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
        .arg(htmlEscape(tr("Bits")),
             htmlEscape(tr("%1 bit(s), last byte valid bits: %2").arg(bitCount).arg(validBits)));
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>")
        .arg(htmlEscape(tr("CRC16-A")),
             htmlEscape(statusText(crc.value(QStringLiteral("checked")).toBool(),
                                   crc.value(QStringLiteral("ok")).toBool())));

    QString parityText = statusText(parity.value(QStringLiteral("checked")).toBool(),
                                    parity.value(QStringLiteral("ok")).toBool());
    const QJsonArray parityBits = parity.value(QStringLiteral("bits")).toArray();
    if (!parityBits.isEmpty())
    {
        QStringList bits;
        for (const QJsonValue &value : parityBits)
            bits << (value.isNull() ? QStringLiteral("-") : QString::number(value.toInt()));
        parityText += QStringLiteral("  ") + bits.join(QLatin1Char(' '));
    }
    html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
        .arg(htmlEscape(tr("Parity")), htmlEscape(parityText));

    if (atsValue.isObject())
    {
        const QJsonObject ats = atsValue.toObject();
        html << QStringLiteral("<tr><td class=\"k\">%1</td><td><code>%2</code></td></tr>")
            .arg(htmlEscape(tr("ATS")), htmlEscape(ats.value(QStringLiteral("hex")).toString()));
    }
    html << QStringLiteral("</table>");

    const QString pwdHex = decode.value(QStringLiteral("pwd_auth_hex")).toString();
    if (!pwdHex.isEmpty())
    {
        html << QStringLiteral("<p style=\"background:#fff3b0;color:#a00000;font-weight:bold;\">%1 "
                              "<code>%2</code> %3</p>")
            .arg(htmlEscape(tr("Plaintext PWD_AUTH (sent in clear on air):")),
                 htmlEscape(pwdHex),
                 htmlEscape(decode.value(QStringLiteral("pwd_auth_value")).toString()));
    }

    const QJsonArray commands = decode.value(QStringLiteral("commands")).toArray();
    if (!commands.isEmpty())
    {
        QStringList lines;
        for (const QJsonValue &value : commands)
            lines << QStringLiteral("→ ") + value.toString();
        html << QStringLiteral("<p><b>%1</b></p><pre>%2</pre>")
            .arg(htmlEscape(tr("Decoded commands")), htmlEscape(lines.join(QLatin1Char('\n'))));
    }

    const QJsonArray extra = decode.value(QStringLiteral("extra")).toArray();
    if (!extra.isEmpty())
    {
        QStringList lines;
        for (const QJsonValue &value : extra)
            lines << value.toString();
        html << QStringLiteral("<p><b>%1</b></p><pre>%2</pre>")
            .arg(htmlEscape(tr("Parser explanation")), htmlEscape(lines.join(QLatin1Char('\n'))));
    }

    const QJsonArray warnings = frame.value(QStringLiteral("warnings")).toArray();
    if (!warnings.isEmpty())
    {
        QStringList lines;
        for (const QJsonValue &value : warnings)
            lines << QStringLiteral("! ") + value.toString();
        html << QStringLiteral("<p style=\"color:#a06000;\"><b>%1</b></p><pre>%2</pre>")
            .arg(htmlEscape(tr("Warnings")), htmlEscape(lines.join(QLatin1Char('\n'))));
    }

    if (!error.isEmpty())
    {
        html << QStringLiteral("<p style=\"color:#b00020;\"><b>%1</b></p><pre>%2</pre>")
            .arg(htmlEscape(tr("Parse error")),
                 htmlEscape(error.value(QStringLiteral("message")).toString()));
    }

    html << QStringLiteral("</body></html>");
    m_details->setHtml(html.join(QString()));
}

void Iso14443aPage::exportJson()
{
    if (m_lastData.isEmpty())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export parsed capture as JSON"), defaultExportName(),
        tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Cannot write the JSON file:\n%1").arg(path));
        return;
    }
    file.write(QJsonDocument(m_lastData).toJson(QJsonDocument::Indented));
    file.close();
    m_statusLabel->setText(tr("Exported to %1").arg(path));
    onServiceLog(tr("Exported %1 frame(s) to %2").arg(m_frames.size()).arg(path));
}

QString Iso14443aPage::defaultExportName() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(dir).filePath(QStringLiteral("iso14443a_%1.json").arg(stamp));
}

QStringList Iso14443aPage::traceSearchDirectories() const
{
    QStringList dirs;
    if (m_traceProvider)
        dirs << m_traceProvider();
    if (!m_clientWorkingDir.isEmpty())
        dirs << m_clientWorkingDir;
    dirs << QDir::homePath() + QStringLiteral("/.proxmark3/traces");
    dirs << QDir::homePath() + QStringLiteral("/.proxmark3");
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!documents.isEmpty())
        dirs << documents;
    dirs.removeDuplicates();
    return dirs;
}

QString Iso14443aPage::findLatestTrace() const
{
    QStringList candidates;

    // 1. Paths already printed by `trace save`/`trace load` in the terminal.
    const QRegularExpression absolutePath(
        QStringLiteral("([A-Za-z]:[\\\\/][^\\r\\n\"'<>|]*?\\.(?:trace|trc))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression relativePath(
        QStringLiteral("((?:\\.{1,2}[\\\\/])?[^\\s\"'<>|]+\\.(?:trace|trc))"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = absolutePath.globalMatch(m_terminalText);
    while (it.hasNext())
    {
        QString path = it.next().captured(1).trimmed();
        while (!path.isEmpty() && QStringLiteral(".,;:)").contains(path.at(path.size() - 1)))
            path.chop(1);
        if (QFileInfo::exists(path))
            candidates << QFileInfo(path).absoluteFilePath();
    }

    it = relativePath.globalMatch(m_terminalText);
    while (it.hasNext())
    {
        QString path = it.next().captured(1).trimmed();
        while (!path.isEmpty() && QStringLiteral(".,;:)").contains(path.at(path.size() - 1)))
            path.chop(1);
        QFileInfo info(path);
        if (!info.isAbsolute())
            info = QFileInfo(QDir(m_clientWorkingDir).filePath(path));
        if (info.exists())
            candidates << info.absoluteFilePath();
    }

    // 2. Newest file in the known trace-save directories.
    for (const QString &directory : traceSearchDirectories())
    {
        QDir dir(directory);
        if (!dir.exists())
            continue;
        const QFileInfoList files = dir.entryInfoList(
            QStringList() << QStringLiteral("*.trace") << QStringLiteral("*.trc"),
            QDir::Files | QDir::Readable, QDir::Time);
        for (const QFileInfo &info : files)
        {
            if (info.exists())
                candidates << info.absoluteFilePath();
        }
    }

    candidates.removeDuplicates();
    QFileInfo best;
    for (const QString &candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (!info.exists() || !info.isFile())
            continue;
        if (best.filePath().isEmpty() || info.lastModified() > best.lastModified())
            best = info;
    }
    return best.filePath();
}

void Iso14443aPage::importLatestTrace()
{
    if (m_busy)
        return;

    // MainWindow fills the terminal text synchronously (same thread).
    emit terminalTextRequested();
    const QString path = findLatestTrace();
    if (path.isEmpty())
    {
        const QString message = tr(
            "No trace file was found.\n\n"
            "Automatic capture is not supported: this page never starts a sniff "
            "session or opens a second Proxmark3 connection. In the terminal run "
            "`hf 14a sniff`, stop it, then run `trace save`, and use "
            "Open PM3 Trace (or retry this button after the file exists).");
        m_statusLabel->setText(tr("No trace file found."));
        QMessageBox::information(this, tr("Import latest trace"), message);
        onServiceLog(tr("Import latest trace: no .trace/.trc file found."));
        return;
    }

    m_sourceKind = TraceSource;
    m_sourcePath = path;
    m_statusLabel->setText(tr("Latest trace: %1").arg(path));
    onServiceLog(tr("Import latest trace: %1").arg(path));
    startParse();
}

QString Iso14443aPage::statusText(bool checked, bool ok) const
{
    if (!checked)
        return na();
    return ok ? tr("OK") : tr("Fail");
}

QString Iso14443aPage::typeLabel(const QString &type) const
{
    if (type == QLatin1String("reqa")) return tr("REQA");
    if (type == QLatin1String("wupa")) return tr("WUPA");
    if (type == QLatin1String("command_7bit")) return tr("7-bit command");
    if (type == QLatin1String("hlta")) return tr("HLTA");
    if (type == QLatin1String("rats")) return tr("RATS");
    if (type == QLatin1String("ats")) return tr("ATS");
    if (type == QLatin1String("pps")) return tr("PPS");
    if (type == QLatin1String("i_block")) return tr("I-block");
    if (type == QLatin1String("r_block")) return tr("R-block");
    if (type == QLatin1String("s_block")) return tr("S-block");
    if (type == QLatin1String("atqa")) return tr("ATQA");
    if (type == QLatin1String("sak")) return tr("SAK");
    if (type == QLatin1String("select")) return tr("SELECT");
    if (type == QLatin1String("anticollision")) return tr("Anticollision");
    if (type == QLatin1String("mifare_auth_a")) return tr("MIFARE Auth A");
    if (type == QLatin1String("mifare_auth_b")) return tr("MIFARE Auth B");
    if (type == QLatin1String("mifare_nonce")) return tr("MIFARE nonce");
    if (type == QLatin1String("mifare_auth_cipher")) return tr("MIFARE auth response");
    if (type == QLatin1String("mifare_auth_challenge")) return tr("MIFARE auth challenge");
    if (type == QLatin1String("mifare_ack")) return tr("MIFARE ACK");
    if (type == QLatin1String("mifare_nak")) return tr("MIFARE NAK");
    if (type == QLatin1String("mifare_read")) return tr("MIFARE read");
    if (type == QLatin1String("mifare_data")) return tr("MIFARE data");
    if (type == QLatin1String("ntag_get_version")) return tr("NTAG GET_VERSION");
    if (type == QLatin1String("ntag_read")) return tr("NTAG read");
    if (type == QLatin1String("ntag_write")) return tr("NTAG write");
    if (type == QLatin1String("ntag_pwd_auth")) return tr("NTAG PWD_AUTH");
    if (type == QLatin1String("ntag")) return tr("NTAG");
    if (type == QLatin1String("desfire")) return tr("DESFire");
    if (type == QLatin1String("fm1208")) return tr("FM1208");
    if (type == QLatin1String("apdu")) return tr("APDU");
    if (type == QLatin1String("other")) return tr("Other");
    return tr("Unknown");
}

QString Iso14443aPage::phaseLabel(const QString &phase) const
{
    if (phase == QLatin1String("14443-3"))
        return tr("ISO 14443-3 (anticollision/select)");
    if (phase == QLatin1String("14443-4"))
        return tr("ISO 14443-4 (T=CL)");
    if (phase == QLatin1String("mifare"))
        return tr("MIFARE Classic");
    if (phase == QLatin1String("native"))
        return tr("NTAG / Ultralight");
    return tr("Unknown");
}

QString Iso14443aPage::directionLabel(const QString &direction) const
{
    if (direction == QLatin1String("pcd_to_picc"))
        return tr("PCD→PICC");
    if (direction == QLatin1String("picc_to_pcd"))
        return tr("PICC→PCD");
    return tr("Unknown");
}

QString Iso14443aPage::elideText(const QString &text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("…");
}

QString Iso14443aPage::asciiOf(const QString &hex)
{
    if (hex.isEmpty())
        return QStringLiteral("—");
    const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
    QString text;
    text.reserve(bytes.size());
    for (char c : bytes)
    {
        const unsigned char value = static_cast<unsigned char>(c);
        text.append(value >= 0x20 && value <= 0x7E ? QChar(value) : QLatin1Char('.'));
    }
    return text;
}

QString Iso14443aPage::htmlEscape(const QString &text)
{
    return text.toHtmlEscaped();
}

int registerIso14443aPage(QTabWidget *tabs, Iso14443aPage *page)
{
    if (!tabs || !page)
        return -1;
    return tabs->addTab(page, Iso14443aPage::pageTitle());
}
