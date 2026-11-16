#include "felicatab.h"

#include "common/util.h"
#include "core/clientsession.h"
#include "felica/cjrc_profile.h"
#include "felica/code_tables.h"
#include "felica/felica_exporter.h"
#include "felica/felica_reader.h"

#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

class NumericItem : public QTableWidgetItem
{
public:
    NumericItem(const QString &text, double value)
        : QTableWidgetItem(text)
        , m_value(value)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const NumericItem *numeric = dynamic_cast<const NumericItem *>(&other);
        if (numeric)
            return m_value < numeric->m_value;
        return QTableWidgetItem::operator<(other);
    }

private:
    double m_value = 0;
};

QString hexBytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ').toUpper());
}

} // namespace

FelicaTab::FelicaTab(Util *util, ClientSession *session, QWidget *parent)
    : QWidget(parent)
    , m_util(util)
    , m_session(session)
{
    m_reader = new FelicaReader(m_session, this);
    connect(m_reader, &FelicaReader::progress, this, &FelicaTab::onProgress);
    connect(m_reader, &FelicaReader::cardDetected, this, &FelicaTab::onCardDetected);
    connect(m_reader, &FelicaReader::finished, this, &FelicaTab::onFinished);
    connect(m_reader, &FelicaReader::failed, this, &FelicaTab::onFailed);

    buildUi();
    resetView();
    if (m_session)
        setClientConnected(m_session->isConnected());
}

FelicaTab::~FelicaTab() = default;

void FelicaTab::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *actions = new QHBoxLayout();
    m_readButton = new QPushButton(tr("Read Card"), this);
    m_readButton->setObjectName(QStringLiteral("Felica_readButton"));
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("Felica_cancelButton"));
    m_exportJsonButton = new QPushButton(tr("Export JSON"), this);
    m_exportJsonButton->setObjectName(QStringLiteral("Felica_exportJsonButton"));
    m_exportCsvButton = new QPushButton(tr("Export CSV"), this);
    m_exportCsvButton->setObjectName(QStringLiteral("Felica_exportCsvButton"));
    m_clearButton = new QPushButton(tr("Clear View"), this);
    m_clearButton->setObjectName(QStringLiteral("Felica_clearButton"));
    m_includeRawCheck = new QCheckBox(tr("Include raw data in JSON"), this);
    m_includeRawCheck->setObjectName(QStringLiteral("Felica_includeRawCheck"));

    actions->addWidget(m_readButton);
    actions->addWidget(m_cancelButton);
    actions->addSpacing(12);
    actions->addWidget(m_exportJsonButton);
    actions->addWidget(m_exportCsvButton);
    actions->addWidget(m_clearButton);
    actions->addStretch();
    actions->addWidget(m_includeRawCheck);
    root->addLayout(actions);

    QFrame *cardFrame = new QFrame(this);
    cardFrame->setFrameShape(QFrame::StyledPanel);
    QHBoxLayout *cardLayout = new QHBoxLayout(cardFrame);

    QVBoxLayout *balanceLayout = new QVBoxLayout();
    m_balanceCaption = new QLabel(tr("Balance"), cardFrame);
    m_balanceCaption->setAlignment(Qt::AlignCenter);
    m_balanceLabel = new QLabel(QStringLiteral("—"), cardFrame);
    QFont balanceFont = m_balanceLabel->font();
    balanceFont.setPointSize(balanceFont.pointSize() + 14);
    balanceFont.setBold(true);
    m_balanceLabel->setFont(balanceFont);
    m_balanceLabel->setAlignment(Qt::AlignCenter);
    balanceLayout->addWidget(m_balanceCaption);
    balanceLayout->addWidget(m_balanceLabel);
    balanceLayout->addStretch();
    cardLayout->addLayout(balanceLayout, 1);

    QFormLayout *infoLayout = new QFormLayout();
    m_cardTypeValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_cardIdValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_pmmValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_icValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_systemsValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_detectedValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_cardIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addRow(tr("Card type:"), m_cardTypeValue);
    infoLayout->addRow(tr("IDm:"), m_cardIdValue);
    infoLayout->addRow(tr("PMm:"), m_pmmValue);
    infoLayout->addRow(tr("IC:"), m_icValue);
    infoLayout->addRow(tr("Systems:"), m_systemsValue);
    infoLayout->addRow(tr("Read at:"), m_detectedValue);
    cardLayout->addLayout(infoLayout, 2);
    root->addWidget(cardFrame);

    QHBoxLayout *progressLayout = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_statusLabel = new QLabel(tr("Connect the Proxmark3 client, then press Read Card."), this);
    m_qualityLabel = new QLabel(this);
    progressLayout->addWidget(m_statusLabel, 1);
    progressLayout->addWidget(m_qualityLabel);
    progressLayout->addWidget(m_progress, 1);
    root->addLayout(progressLayout);

    m_tabs = new QTabWidget(this);

    // Transactions
    QWidget *historyPage = new QWidget(m_tabs);
    QVBoxLayout *historyLayout = new QVBoxLayout(historyPage);
    QSplitter *historySplitter = new QSplitter(Qt::Vertical, historyPage);
    m_historyTable = new QTableWidget(historySplitter);
    m_historyTable->setObjectName(QStringLiteral("Felica_historyTable"));
    m_historyTable->setColumnCount(10);
    m_historyTable->setHorizontalHeaderLabels(QStringList()
        << tr("#") << tr("Date") << tr("Time") << tr("Type") << tr("Entry")
        << tr("Exit") << tr("Terminal") << tr("Amount") << tr("Balance") << tr("TX#"));
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->setSortingEnabled(true);
    m_historyTable->horizontalHeader()->setStretchLastSection(false);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyDetail = new QPlainTextEdit(historySplitter);
    m_historyDetail->setReadOnly(true);
    m_historyDetail->setPlaceholderText(tr("Select a transaction to inspect its raw bytes."));
    historySplitter->addWidget(m_historyTable);
    historySplitter->addWidget(m_historyDetail);
    historySplitter->setStretchFactor(0, 3);
    historySplitter->setStretchFactor(1, 1);
    historyLayout->addWidget(historySplitter);
    m_tabs->addTab(historyPage, tr("Transactions"));

    // Gates
    QWidget *gatePage = new QWidget(m_tabs);
    QVBoxLayout *gateLayout = new QVBoxLayout(gatePage);
    QSplitter *gateSplitter = new QSplitter(Qt::Vertical, gatePage);
    m_gateTable = new QTableWidget(gateSplitter);
    m_gateTable->setObjectName(QStringLiteral("Felica_gateTable"));
    m_gateTable->setColumnCount(7);
    m_gateTable->setHorizontalHeaderLabels(QStringList()
        << tr("#") << tr("Date") << tr("Time") << tr("In / Out") << tr("Station")
        << tr("Amount") << tr("Device"));
    m_gateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_gateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_gateTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_gateTable->setAlternatingRowColors(true);
    m_gateTable->setSortingEnabled(true);
    m_gateTable->horizontalHeader()->setStretchLastSection(false);
    m_gateTable->verticalHeader()->setVisible(false);
    m_gateDetail = new QPlainTextEdit(gateSplitter);
    m_gateDetail->setReadOnly(true);
    m_gateDetail->setPlaceholderText(tr("Select a gate record to inspect its raw bytes."));
    gateSplitter->addWidget(m_gateTable);
    gateSplitter->addWidget(m_gateDetail);
    gateSplitter->setStretchFactor(0, 3);
    gateSplitter->setStretchFactor(1, 1);
    gateLayout->addWidget(gateSplitter);
    m_tabs->addTab(gatePage, tr("Gate history"));

    // Services
    QWidget *servicePage = new QWidget(m_tabs);
    QVBoxLayout *serviceLayout = new QVBoxLayout(servicePage);
    m_serviceTable = new QTableWidget(servicePage);
    m_serviceTable->setObjectName(QStringLiteral("Felica_serviceTable"));
    m_serviceTable->setColumnCount(6);
    m_serviceTable->setHorizontalHeaderLabels(QStringList()
        << tr("Service") << tr("Name") << tr("Access") << tr("Blocks")
        << tr("Read") << tr("Status"));
    m_serviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_serviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_serviceTable->setAlternatingRowColors(true);
    m_serviceTable->horizontalHeader()->setStretchLastSection(true);
    m_serviceTable->verticalHeader()->setVisible(false);
    serviceLayout->addWidget(m_serviceTable);
    m_tabs->addTab(servicePage, tr("Services"));

    // Raw
    QWidget *rawPage = new QWidget(m_tabs);
    QVBoxLayout *rawLayout = new QVBoxLayout(rawPage);
    m_rawTable = new QTableWidget(rawPage);
    m_rawTable->setObjectName(QStringLiteral("Felica_rawTable"));
    m_rawTable->setColumnCount(3);
    m_rawTable->setHorizontalHeaderLabels(QStringList()
        << tr("Service") << tr("Block") << tr("Data (hex)"));
    m_rawTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rawTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rawTable->setAlternatingRowColors(true);
    m_rawTable->horizontalHeader()->setStretchLastSection(true);
    m_rawTable->verticalHeader()->setVisible(false);
    rawLayout->addWidget(m_rawTable);
    m_tabs->addTab(rawPage, tr("Raw data"));

    root->addWidget(m_tabs, 1);

    connect(m_readButton, &QPushButton::clicked, this, &FelicaTab::readCard);
    connect(m_cancelButton, &QPushButton::clicked, this, &FelicaTab::cancelRead);
    connect(m_exportJsonButton, &QPushButton::clicked, this, &FelicaTab::exportJson);
    connect(m_exportCsvButton, &QPushButton::clicked, this, &FelicaTab::exportCsv);
    connect(m_clearButton, &QPushButton::clicked, this, &FelicaTab::clearView);
    connect(m_historyTable, &QTableWidget::itemSelectionChanged,
            this, &FelicaTab::onHistorySelectionChanged);
    connect(m_gateTable, &QTableWidget::itemSelectionChanged,
            this, &FelicaTab::onGateSelectionChanged);
}

void FelicaTab::setClientConnected(bool connected)
{
    m_connected = connected;
    m_readButton->setEnabled(connected && !m_reader->isRunning());
    if (!connected && !m_reader->isRunning())
    {
        m_statusLabel->setText(tr("Proxmark3 client is not connected."));
    }
}

void FelicaTab::setBusy(bool busy)
{
    m_readButton->setEnabled(m_connected && !busy);
    m_cancelButton->setEnabled(busy);
    m_exportJsonButton->setEnabled(!busy && m_hasResult);
    m_exportCsvButton->setEnabled(!busy && m_hasResult);
    m_clearButton->setEnabled(!busy && m_hasResult);
}

void FelicaTab::resetView()
{
    m_result = Transit::ReadResult();
    m_hasResult = false;

    m_balanceLabel->setText(QStringLiteral("—"));
    m_cardTypeValue->setText(QStringLiteral("—"));
    m_cardIdValue->setText(QStringLiteral("—"));
    m_pmmValue->setText(QStringLiteral("—"));
    m_icValue->setText(QStringLiteral("—"));
    m_systemsValue->setText(QStringLiteral("—"));
    m_detectedValue->setText(QStringLiteral("—"));
    m_qualityLabel->clear();
    m_progress->setValue(0);

    m_historyTable->setRowCount(0);
    m_gateTable->setRowCount(0);
    m_serviceTable->setRowCount(0);
    m_rawTable->setRowCount(0);
    m_historyDetail->clear();
    m_gateDetail->clear();

    setBusy(false);
}

void FelicaTab::clearView()
{
    resetView();
    m_statusLabel->setText(tr("Cleared. Press Read Card to read the card again."));
}

void FelicaTab::readCard()
{
    if (!m_session || !m_session->isConnected())
    {
        m_statusLabel->setText(tr("Proxmark3 client is not connected."));
        return;
    }

    m_statusLabel->setText(tr("Starting read..."));
    m_progress->setValue(0);
    setBusy(true);
    m_reader->start();
    setBusy(m_reader->isRunning());
}

void FelicaTab::cancelRead()
{
    if (!m_reader->isRunning())
        return;
    m_statusLabel->setText(tr("Cancelling..."));
    m_reader->cancel();
}

void FelicaTab::onProgress(const QString &message, int percent)
{
    m_statusLabel->setText(message);
    if (percent >= 0)
        m_progress->setValue(qBound(0, percent, 100));
}

void FelicaTab::onCardDetected(const Transit::CardInfo &card)
{
    m_result.card = card;
    updateCardPanel();
}

void FelicaTab::onFinished(const Transit::ReadResult &result)
{
    m_result = result;
    m_hasResult = !result.transactions.isEmpty() || result.balance.valid;
    populate(result);
    setBusy(false);

    if (result.cancelled)
        m_statusLabel->setText(tr("Read cancelled; showing partial data."));
    else if (result.partial)
        m_statusLabel->setText(tr("Read finished with warnings; some services were not read."));
    else
        m_statusLabel->setText(tr("Read complete."));
    m_progress->setValue(100);
}

void FelicaTab::onFailed(const QString &message)
{
    setBusy(false);
    m_progress->setValue(0);
    m_statusLabel->setText(message);
}

void FelicaTab::updateCardPanel()
{
    const Transit::CardInfo &card = m_result.card;
    m_cardTypeValue->setText(card.cardType.isEmpty() ? QStringLiteral("—") : card.cardType);
    m_cardIdValue->setText(card.cardId.isEmpty() ? QStringLiteral("—") : card.cardId);
    m_pmmValue->setText(card.pmm.isEmpty() ? QStringLiteral("—") : card.pmm);
    m_icValue->setText(card.icCode.isEmpty() ? QStringLiteral("—")
                                             : QStringLiteral("%1 %2").arg(card.icCode, card.icName));
    m_systemsValue->setText(card.systems.isEmpty() ? QStringLiteral("—")
                                                   : card.systems.join(QStringLiteral(", ")));
    m_detectedValue->setText(card.detectedAt.isEmpty() ? QStringLiteral("—") : card.detectedAt);

    if (m_result.balance.valid)
    {
        m_balanceLabel->setText(QStringLiteral("%1 %2")
                                    .arg(QStringLiteral("¥"))
                                    .arg(m_result.balance.amount));
    }
    else
    {
        m_balanceLabel->setText(QStringLiteral("—"));
    }
}

QString FelicaTab::formatAmount(qint64 value, bool known) const
{
    if (!known)
        return QStringLiteral("?");
    if (value > 0)
        return QStringLiteral("+%1").arg(value);
    return QString::number(value);
}

QString FelicaTab::stationText(const Transit::StationInfo &station) const
{
    if (!station.resolved)
        return QString();
    QString text = station.primaryName;
    if (station.ambiguous)
    {
        QStringList others;
        for (const Transit::StationCandidate &candidate : station.candidates)
        {
            if (!candidate.station.isEmpty() && candidate.station != station.primaryName
                && !others.contains(candidate.station))
            {
                others.append(candidate.station);
            }
        }
        if (!others.isEmpty())
            text += QStringLiteral(" (+%1)").arg(others.size());
    }
    return text;
}

QString FelicaTab::candidateText(const Transit::StationInfo &station) const
{
    if (station.candidates.isEmpty())
        return station.rawCode.isEmpty() ? QString()
                                         : tr("Unresolved code: %1").arg(station.rawCode);

    QStringList lines;
    for (const Transit::StationCandidate &candidate : station.candidates)
    {
        lines.append(QStringLiteral("%1 / %2 / %3 [%4]")
                         .arg(candidate.company, candidate.line, candidate.station,
                              candidate.table));
    }
    return lines.join(QLatin1Char('\n'));
}

QString FelicaTab::tableCell(const Transit::StationInfo &station) const
{
    const QString text = stationText(station);
    if (!text.isEmpty())
        return text;
    if (!station.rawCode.isEmpty())
        return tr("Unknown (%1)").arg(station.rawCode);
    return QStringLiteral("—");
}

QString FelicaTab::timestampOf(const Transit::TransactionRecord &record) const
{
    if (!record.timeKnown)
        return QString();
    return record.time;
}

void FelicaTab::populate(const Transit::ReadResult &result)
{
    updateCardPanel();

    QStringList quality;
    if (result.partial)
        quality.append(tr("partial"));
    if (!result.warnings.isEmpty())
        quality.append(tr("%1 warning(s)").arg(result.warnings.size()));
    m_qualityLabel->setText(quality.join(QStringLiteral(" · ")));

    populateHistory();
    populateGates();
    populateServices();
    populateRaw();
}

void FelicaTab::populateHistory()
{
    m_historyTable->setSortingEnabled(false);
    m_historyTable->setRowCount(m_result.transactions.size());

    for (int row = 0; row < m_result.transactions.size(); ++row)
    {
        const Transit::TransactionRecord &record = m_result.transactions.at(row);

        m_historyTable->setItem(row, 0, new NumericItem(QString::number(record.index), record.index));
        m_historyTable->setItem(row, 1, new QTableWidgetItem(record.date));

        const QString time = timestampOf(record);
        QString timeText = time.isEmpty() ? QStringLiteral("—") : time;
        if (record.timeSource == QStringLiteral("gate"))
            timeText += QLatin1Char('*');
        QTableWidgetItem *timeItem = new QTableWidgetItem(timeText);
        if (record.timeSource == QStringLiteral("gate"))
            timeItem->setToolTip(tr("Time derived from the matching gate record (0x108F)."));
        m_historyTable->setItem(row, 2, timeItem);

        m_historyTable->setItem(row, 3, new QTableWidgetItem(record.transactionLabel));
        m_historyTable->setItem(row, 4, new QTableWidgetItem(tableCell(record.entry)));
        m_historyTable->setItem(row, 5, new QTableWidgetItem(tableCell(record.exit)));
        m_historyTable->setItem(row, 6, new QTableWidgetItem(record.terminalLabel));

        NumericItem *amount = new NumericItem(formatAmount(record.delta, record.deltaKnown),
                                              static_cast<double>(record.delta));
        if (record.deltaKnown && record.delta < 0)
            amount->setForeground(QColor(0xC0, 0x39, 0x2B));
        else if (record.deltaKnown && record.delta > 0)
            amount->setForeground(QColor(0x27, 0xAE, 0x60));
        m_historyTable->setItem(row, 7, amount);

        m_historyTable->setItem(row, 8, new NumericItem(QString::number(record.balance),
                                                         static_cast<double>(record.balance)));
        m_historyTable->setItem(row, 9, new NumericItem(QString::number(record.transactionNumber),
                                                         record.transactionNumber));
    }

    m_historyTable->setSortingEnabled(true);
    m_historyTable->resizeColumnsToContents();
    m_historyDetail->clear();
}

void FelicaTab::populateGates()
{
    m_gateTable->setSortingEnabled(false);
    m_gateTable->setRowCount(m_result.gates.size());

    for (int row = 0; row < m_result.gates.size(); ++row)
    {
        const Transit::GateRecord &record = m_result.gates.at(row);
        m_gateTable->setItem(row, 0, new NumericItem(QString::number(record.index), record.index));
        m_gateTable->setItem(row, 1, new QTableWidgetItem(record.date));
        m_gateTable->setItem(row, 2, new QTableWidgetItem(record.timeKnown ? record.time : QStringLiteral("—")));
        m_gateTable->setItem(row, 3, new QTableWidgetItem(record.directionLabel));
        m_gateTable->setItem(row, 4, new QTableWidgetItem(tableCell(record.station)));
        m_gateTable->setItem(row, 5, new NumericItem(QString::number(record.amount),
                                                     static_cast<double>(record.amount)));
        m_gateTable->setItem(row, 6, new QTableWidgetItem(record.deviceId));
    }

    m_gateTable->setSortingEnabled(true);
    m_gateTable->resizeColumnsToContents();
    m_gateDetail->clear();
}

void FelicaTab::populateServices()
{
    m_serviceTable->setRowCount(m_result.services.size());
    for (int row = 0; row < m_result.services.size(); ++row)
    {
        const Transit::ServiceReadStatus &status = m_result.services.at(row);
        m_serviceTable->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(status.code, 4, 16, QLatin1Char('0')).toUpper()
                .replace(QStringLiteral("0X"), QStringLiteral("0x"))));
        m_serviceTable->setItem(row, 1, new QTableWidgetItem(status.name));
        m_serviceTable->setItem(row, 2, new QTableWidgetItem(FelicaCodes::serviceAccess(status.code)));
        m_serviceTable->setItem(row, 3, new NumericItem(QString::number(status.blocksExpected),
                                                         status.blocksExpected));
        m_serviceTable->setItem(row, 4, new NumericItem(QString::number(status.blocksRead),
                                                         status.blocksRead));

        QString text;
        if (!status.attempted)
            text = tr("not read");
        else if (status.success)
            text = tr("OK");
        else
            text = status.error.isEmpty() ? tr("failed") : tr("failed: %1").arg(status.error);
        m_serviceTable->setItem(row, 5, new QTableWidgetItem(text));
    }
    m_serviceTable->resizeColumnsToContents();
}

void FelicaTab::populateRaw()
{
    m_rawTable->setRowCount(m_result.rawBlocks.size());
    for (int row = 0; row < m_result.rawBlocks.size(); ++row)
    {
        const Transit::RawBlock &block = m_result.rawBlocks.at(row);
        m_rawTable->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(block.service, 4, 16, QLatin1Char('0')).toUpper()
                .replace(QStringLiteral("0X"), QStringLiteral("0x"))));
        m_rawTable->setItem(row, 1, new NumericItem(
            QStringLiteral("%1").arg(block.block, 4, 16, QLatin1Char('0')).toUpper(), block.block));
        m_rawTable->setItem(row, 2, new QTableWidgetItem(hexBytes(block.data)));
    }
    m_rawTable->resizeColumnsToContents();
}

void FelicaTab::onHistorySelectionChanged()
{
    const int row = m_historyTable->currentRow();
    if (row < 0 || row >= m_result.transactions.size())
    {
        m_historyDetail->clear();
        return;
    }

    // Rows may have been re-sorted; recover the record index from column 0.
    const int index = m_historyTable->item(row, 0)
                          ? m_historyTable->item(row, 0)->text().toInt()
                          : row;
    if (index < 0 || index >= m_result.transactions.size())
        return;

    const Transit::TransactionRecord &record = m_result.transactions.at(index);
    QStringList lines;
    lines.append(tr("Raw: %1").arg(hexBytes(record.raw)));
    lines.append(tr("Terminal: %1").arg(record.terminalLabel));
    lines.append(tr("Transaction: %1").arg(record.transactionLabel));
    lines.append(tr("Payment: %1").arg(record.paymentLabel));
    lines.append(tr("Gate instruction: %1").arg(record.gateInstructionLabel));
    if (record.hasPosId)
        lines.append(tr("POS ID: %1 (no public merchant mapping)").arg(record.posIdHex));
    if (record.entry.resolved)
    {
        lines.append(tr("Entry candidates:"));
        lines.append(candidateText(record.entry));
    }
    if (record.exit.resolved)
    {
        lines.append(tr("Exit candidates:"));
        lines.append(candidateText(record.exit));
    }
    if (!record.deltaKnown)
        lines.append(tr("Amount: unknown (oldest record in the 20-record window)"));
    m_historyDetail->setPlainText(lines.join(QLatin1Char('\n')));
}

void FelicaTab::onGateSelectionChanged()
{
    const int row = m_gateTable->currentRow();
    if (row < 0 || row >= m_result.gates.size())
    {
        m_gateDetail->clear();
        return;
    }

    const int index = m_gateTable->item(row, 0) ? m_gateTable->item(row, 0)->text().toInt() : row;
    if (index < 0 || index >= m_result.gates.size())
        return;

    const Transit::GateRecord &record = m_result.gates.at(index);
    QStringList lines;
    lines.append(tr("Raw: %1").arg(hexBytes(record.raw)));
    lines.append(tr("Raw station code: %1").arg(record.station.rawCode));
    lines.append(tr("Station candidates:"));
    lines.append(candidateText(record.station));
    lines.append(tr("Device: %1").arg(record.deviceId));
    m_gateDetail->setPlainText(lines.join(QLatin1Char('\n')));
}

void FelicaTab::exportJson()
{
    if (!m_hasResult)
        return;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(base).filePath(
        QStringLiteral("felica_%1.json")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"))));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export JSON"), suggested,
                                                      tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return;
    }
    file.write(FelicaExport::toJson(m_result, m_includeRawCheck->isChecked()));
    file.close();
    m_statusLabel->setText(tr("Exported JSON: %1").arg(path));
}

void FelicaTab::exportCsv()
{
    if (!m_hasResult)
        return;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(base).filePath(
        QStringLiteral("felica_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"))));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), suggested,
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    const QString stem = path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
                             ? path.left(path.size() - 4)
                             : path;
    const QString historyPath = stem + QStringLiteral("_transactions.csv");
    const QString gatePath = stem + QStringLiteral("_gates.csv");

    auto writeFile = [this](const QString &filePath, const QByteArray &content) -> bool
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, tr("Export failed"), file.errorString());
            return false;
        }
        file.write(content);
        file.close();
        return true;
    };

    if (!writeFile(historyPath, FelicaExport::historyCsv(m_result))
        || !writeFile(gatePath, FelicaExport::gateCsv(m_result)))
    {
        return;
    }

    m_statusLabel->setText(tr("Exported CSV: %1, %2").arg(historyPath, gatePath));
}
